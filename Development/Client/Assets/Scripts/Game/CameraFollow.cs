using UnityEngine;
using Client.Managers;

namespace Client.Game
{
    /// <summary>
    /// 카메라가 LocalPlayer (내 캐릭터) 를 부드럽게 따라가게 한다.
    ///
    /// 동작:
    /// - 시작 시 StageManager.Instance.LocalPlayer 가 생성될 때까지 대기.
    /// - 생성되면 매 프레임 그 캐릭터의 위치 + offset 으로 카메라를 부드럽게 이동.
    /// - 회전(yaw)은 캐릭터를 따라가지 않음 (쿼터뷰 클릭 무브 게임의 표준 방식).
    /// - 휠 줌은 2단계다:
    ///     [1단계] distance 를 max→min 으로 당김 (일반 쿼터뷰 줌).
    ///     [2단계] min 에서 더 당기면 pitch 가 눕어(예 55°→15°) 캐릭터 정면을 볼 수 있는 거의 수평인 시점으로 전환
    /// - 캐릭터가 사라지면 (스테이지 이동 등) 자동으로 다시 대기 모드로.
    ///
    /// 사용법:
    /// - MainCamera 에 이 컴포넌트를 붙인다.
    /// - 인스펙터에서 pitch/yaw/distance 만 조정하면 적절한 쿼터뷰가 나온다.
    /// - 정면 전환 튜닝은 Front View 섹션(m_lowPitchDeg 등)에서.
    /// - 카메라의 transform 회전은 매 프레임 정면 전환 진행도에 따라 재계산된다.
    /// </summary>
    public class CameraFollow : MonoBehaviour
    {
        [Header("View Angle")]
        [Tooltip("아래로 내려다보는 각도 (도). 45~55 가 쿼터뷰 표준.")]
        [SerializeField] private float m_pitchDeg = 45;

        [Tooltip("수평 회전 각도 (도). 0 이면 정북향, 45 면 북동향.")]
        [SerializeField] private float m_yawDeg = 45f;

        [Tooltip("캐릭터로부터의 거리 (m). 휠 입력으로 변경됨. 인스펙터에서 설정한 값은 시작 distance 로 쓰임.")]
        [SerializeField] private float m_distance = 12f;

        [Header("Zoom")]
        [Tooltip("휠 한 칸당 distance 변화량 (m). 2m 가 표준.")]
        [SerializeField] private float m_zoomStep = 2f;

        [Tooltip("줄 인(가까워지기) 한계. distance 최소값.")]
        [SerializeField] private float m_minDistance = 4f;

        [Tooltip("줄 아웃(멀어지기) 한계. distance 최대값.")]
        [SerializeField] private float m_maxDistance = 16f;

        [Tooltip("줄 변경이 실제 distance 에 반영되는 부드러움 시간 상수 (초). 0.1~0.3 권장.")]
        [SerializeField] private float m_zoomSmoothTime = 0.15f;

        [Header("Front View (2단계 줌)")]
        [Tooltip("1단계 줌인이 min 에 닿은 뒤 계속 휠을 굴리면 pitch 가 이 값까지 눕는다. 작을수록 수평(정면)에 가까움. 15~25 권장.")]
        [SerializeField] private float m_lowPitchDeg = 0f;

        [Tooltip("정면 전환 휠 한 칸당 진행도 증가량 (0~1). 0.34 면 3칸에 완전 전환.")]
        [SerializeField] private float m_frontBlendStep = 0.34f;

        [Tooltip("정면 전환 진행도가 실제로 반영되는 부드러움 시간 상수 (초). 0.1~0.3 권장.")]
        [SerializeField] private float m_frontBlendSmoothTime = 0.15f;

        [Header("Follow")]
        [Tooltip("카메라의 시선이 향하는 점에 추가할 오프셋. 캐릭터의 머리 위쪽을 보고 싶으면 y 를 살짝 올린다.")]
        [SerializeField] private Vector3 m_lookOffset = new Vector3(0f, 1.0f, 0f);

        [Tooltip("정면 전환이 최대일 때의 시선 오프셋. pitch 가 눕으면 발끝이 잡히므로 몸통 높이로 올린다.")]
        [SerializeField] private Vector3 m_lowLookOffset = new Vector3(0f, 1.2f, 0f);

        [Tooltip("부드러운 추적의 시간 상수 (초). 작을수록 단단하게 붙고, 크면 느리게 따라옴. 0.1~0.3 추천.")]
        [SerializeField] private float m_smoothTime = 0f;

        // 현재 적용되는 distance. 휠 입력으로 m_targetDistance 가 변하고,
        // m_currentDistance 가 SmoothDamp 로 따라갈. 결과적으로 카메라가 부드럽게 줄인/아웃.
        private float m_targetDistance;
        private float m_currentDistance;

        // m_currentDistance SmoothDamp 용 내부 상태 (속도).
        private float m_distanceVelocity;

        // 2단계(정면 전환) 진행도 0~1. 1단계 줌인이 min 에 닿은 뒤 추가 휠 입력으로 증가한다.
        // 이 값(b)만큼 pitch 가 m_pitchDeg → m_lowPitchDeg 로 눕고 lookOffset 도 보간된다.
        // 카메라는 여전히 "월드 고정 방향 + 위치 추종" 이라 캐릭터 facing 은 참조하지 않는다.
        private float m_targetFrontBlend;
        private float m_currentFrontBlend;
        private float m_frontBlendVelocity;   // m_currentFrontBlend SmoothDamp 용 내부 상태 (속도).

        // SmoothDamp 의 내부 상태 (속도). Unity 의 SmoothDamp 가 부드러운 추적을 위해 ref 로 받는 변수.
        private Vector3 m_velocity = Vector3.zero;

        private void Start()
        {
            // distance 초기값 설정. 인스펙터에서 설정한 m_distance 를 그대로 사용.
            // (범위를 벗어나면 안전하게 클램프)
            m_targetDistance = Mathf.Clamp(m_distance, m_minDistance, m_maxDistance);
            m_currentDistance = m_targetDistance;

            // 시작은 항상 순수 쿼터뷰(정면 전환 0).
            m_targetFrontBlend = 0f;
            m_currentFrontBlend = 0f;

            // 회전 초기값(정면 전환 전 = 순수 쿼터뷰). target 이 아직 없어도 최소한
            // 올바른 방향은 보고 있도록 한 번 세팅한다. 이후 매 프레임 재계산된다.
            transform.rotation = Quaternion.Euler(m_pitchDeg, m_yawDeg, 0f);

            // 시작 시점에 LocalPlayer 가 이미 있으면 즉시 위치/회전 스냅 (부드러운 추적 없이).
            // 안 그러면 카메라가 원점에서 캐릭터까지 천천히 끌려오는 부자연스러움이 생김.
            PlayerCharacter target = getTarget();
            if (target != null)
            {
                computeDesiredPose(target, m_currentDistance, m_currentFrontBlend,
                    out Vector3 pos, out Quaternion rot);
                transform.position = pos;
                transform.rotation = rot;
            }
        }

        private void LateUpdate()
        {
            // LateUpdate 에서 카메라를 갱신하는 이유:
            // - PlayerCharacter.Update() 가 캐릭터 위치를 먼저 갱신함 (Update 단계)
            // - 그 후 LateUpdate 에서 카메라를 갱신해야 "이번 프레임의 캐릭터 위치" 를 정확히 따라감
            // - Update 에서 하면 카메라가 1 프레임 뒤처질 수 있음 (덜덜거림의 원인)

            // 1. 휠 입력으로 목표 distance / 정면 전환 진행도 갱신.
            processZoomInput();

            // 2. 실제 distance 가 목표를 따라가도록 부드럽게 보간.
            m_currentDistance = Mathf.SmoothDamp(
                m_currentDistance,
                m_targetDistance,
                ref m_distanceVelocity,
                m_zoomSmoothTime);

            // 2-1. 정면 전환 진행도도 부드럽게 보간. 이 값이 보간되므로 pitch/회전도 자연히 부드럽다.
            m_currentFrontBlend = Mathf.SmoothDamp(
                m_currentFrontBlend,
                m_targetFrontBlend,
                ref m_frontBlendVelocity,
                m_frontBlendSmoothTime);

            // 3. 캐릭터 따라가기.
            PlayerCharacter target = getTarget();
            if (target == null)
                return;

            computeDesiredPose(target, m_currentDistance, m_currentFrontBlend,
                out Vector3 desiredPos, out Quaternion desiredRot);

            // SmoothDamp: 목표 위치까지 부드럽게 보간.
            transform.position = Vector3.SmoothDamp(
                transform.position,
                desiredPos,
                ref m_velocity,
                m_smoothTime);

            // 회전은 정면 전환에 따라 매 프레임 바뀌므로 여기서 세팅한다.
            // (전환 진행도 자체가 위에서 SmoothDamp 로 보간되므로 회전도 부드럽게 눕는다.)
            transform.rotation = desiredRot;

            applyShake();   // 피격 등으로 등록된 흔들림을 최종 위치에 더한다 (타격감).
        }

        // ── 카메라 흔들림 (타격감) ──
        // 피격 시 SkillSystem 이 AddShake 로 등록. LateUpdate 최종 위치에 화면평면(카메라 로컬 X/Y) 랜덤 오프셋을 더하고 시간 감쇠.
        [SerializeField] private float m_shakeDecay = 1.0f;   // 초당 감쇠량(유닛/초)
        private float m_shakeMagnitude;

        public void AddShake(float magnitude)
        {
            if (magnitude > m_shakeMagnitude)   // 더 강한 흔들림으로만 갱신(겹쳐도 과하지 않게)
                m_shakeMagnitude = magnitude;
        }

        private void applyShake()
        {
            if (m_shakeMagnitude <= 0.0001f)
            {
                m_shakeMagnitude = 0f;
                return;
            }
            Vector2 r = Random.insideUnitCircle * m_shakeMagnitude;
            transform.position += transform.right * r.x + transform.up * r.y;   // 카메라 로컬 평면 = 화면 흔들림
            m_shakeMagnitude = Mathf.MoveTowards(m_shakeMagnitude, 0f, m_shakeDecay * Time.deltaTime);
        }

        // 휠 델타를 읽어 목표 distance / 정면 전환 진행도를 갱신.
        // 휠을 앞으로 굴리면 (scrollY 양수) 줄인, 뒤로 굴리면 (scrollY 음수) 줄아웃.
        private void processZoomInput()
        {
            if (Managers.Managers.Instance == null)
                return;

            float scrollY = Managers.Managers.Input.MouseScrollDelta.y;
            if (scrollY == 0f)
                return;

            // 휠 델타는 마우스마다 다른 단위로 들어올 수 있으므로 (보통 ±120), 부호만 사용.
            // 줌 축은 2단계로 이어져 있다:
            //   [1단계] distance 를 max→min 으로 당김 (기존 쿼터뷰 줌).
            //   [2단계] distance 가 min 에 닿은 뒤 더 당기면 정면 전환(frontBlend)이 진행.
            // 되돌릴 때는 역순: 먼저 frontBlend 를 풀고, 그 다음 distance 를 멀린다.
            // 이 순서 덕분에 frontBlend 가 0 보다 커지는 시점은 항상 distance==min 이라 이음매가 매끄럽다.
            bool zoomIn = scrollY > 0f;
            if (zoomIn)
            {
                if (m_targetDistance > m_minDistance + 0.01f)
                    m_targetDistance = Mathf.Clamp(m_targetDistance - m_zoomStep, m_minDistance, m_maxDistance);
                else
                    m_targetFrontBlend = Mathf.Clamp01(m_targetFrontBlend + m_frontBlendStep);
            }
            else
            {
                if (m_targetFrontBlend > 0f)
                    m_targetFrontBlend = Mathf.Clamp01(m_targetFrontBlend - m_frontBlendStep);
                else
                    m_targetDistance = Mathf.Clamp(m_targetDistance + m_zoomStep, m_minDistance, m_maxDistance);
            }
        }

        // 주어진 distance / 정면 전환 진행도로부터 카메라의 목표 위치·회전을 계산한다.
        // frontBlend=0 이면 순수 쿼터뷰(기존 동작과 비트 단위로 동일), 1 이면 pitch 가 눕은 정면 시점.
        // 어느 경우에도 카메라는 "월드 고정 방향(yaw 고정) + 위치만 추종" 이다.
        // 따라서 캐릭터를 제자리에서 회전시켜도 카메라는 움직이지 않고,
        // 정면/뒷면을 보려면 플레이어가 캐릭터를 카메라 쪽/반대쪽으로 이동시켜 facing 을 바꿔야 한다.
        private void computeDesiredPose(PlayerCharacter target, float distance, float frontBlend,
            out Vector3 pos, out Quaternion rot)
        {
            // smoothstep 으로 전환 양끝을 부드럽게(가감속).
            float b = frontBlend * frontBlend * (3f - 2f * frontBlend);

            // pitch 만 눕힌다. yaw 는 고정 → 카메라는 캐릭터를 따라 돌지 않는다.
            float effPitch = Mathf.Lerp(m_pitchDeg, m_lowPitchDeg, b);
            rot = Quaternion.Euler(effPitch, m_yawDeg, 0f);

            // forward 가 "보는 방향" 이니, 카메라는 캐릭터에서 -forward 방향으로 떨어져 있어야 함.
            Vector3 offsetDir = -(rot * Vector3.forward);

            // 시선이 향하는 점. pitch 가 눕을수록 발끝이 잡히므로 몸통 높이로 오프셋을 보간.
            Vector3 lookOffset = Vector3.Lerp(m_lookOffset, m_lowLookOffset, b);

            pos = target.transform.position + lookOffset + offsetDir * distance;
        }

        /// <summary>
        /// 현재 추적 대상 (LocalPlayer) 을 반환. 아직 없으면 null.
        /// 캐릭터가 동적으로 생성/제거되므로 매번 StageManager 에서 가져온다.
        /// </summary>
        private PlayerCharacter getTarget()
        {
            StageManager stage = StageManager.Instance;
            if (stage == null)
                return null;
            return stage.LocalPlayer;
        }
    }
}
