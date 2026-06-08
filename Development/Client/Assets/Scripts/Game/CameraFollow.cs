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
    /// - 회전은 따라가지 않음 (쿼터뷰 클릭 무브 게임의 표준 방식).
    /// - 캐릭터가 사라지면 (스테이지 이동 등) 자동으로 다시 대기 모드로.
    ///
    /// 사용법:
    /// - MainCamera 에 이 컴포넌트를 붙인다.
    /// - 인스펙터에서 pitch/yaw/distance 만 조정하면 적절한 쿼터뷰가 나온다.
    /// - 카메라의 transform 회전은 Start() 에서 자동으로 pitch/yaw 로 세팅.
    /// </summary>
    public class CameraFollow : MonoBehaviour
    {
        [Header("View Angle")]
        [Tooltip("아래로 내려다보는 각도 (도). 45~55 가 쿼터뷰 표준.")]
        [SerializeField] private float m_pitchDeg = 55f;

        [Tooltip("수평 회전 각도 (도). 0 이면 정북향, 45 면 북동향. 트오세 느낌은 45 근처.")]
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

        [Header("Follow")]
        [Tooltip("카메라의 시선이 향하는 점에 추가할 오프셋. 캐릭터의 머리 위쪽을 보고 싶으면 y 를 살짝 올린다.")]
        [SerializeField] private Vector3 m_lookOffset = new Vector3(0f, 1.0f, 0f);

        [Tooltip("부드러운 추적의 시간 상수 (초). 작을수록 단단하게 붙고, 크면 느리게 따라옴. 0.1~0.3 추천.")]
        [SerializeField] private float m_smoothTime = 0f;

        // 캐릭터로부터 카메라로의 방향 벡터 (yaw/pitch 로부터 계산). Start 에서 1회 계산.
        // 단위 벡터로 저장 (크기 1). 실제 적용 시 m_currentDistance 를 곱한다.
        private Vector3 m_cameraOffsetDir;

        // 현재 적용되는 distance. 휠 입력으로 m_targetDistance 가 변하고,
        // m_currentDistance 가 SmoothDamp 로 따라갈. 결과적으로 카메라가 부드럽게 줄인/아웃.
        private float m_targetDistance;
        private float m_currentDistance;

        // m_currentDistance SmoothDamp 용 내부 상태 (속도).
        private float m_distanceVelocity;

        // SmoothDamp 의 내부 상태 (속도). Unity 의 SmoothDamp 가 부드러운 추적을 위해 ref 로 받는 변수.
        private Vector3 m_velocity = Vector3.zero;

        private void Start()
        {
            // 1. pitch/yaw 로부터 카메라 방향을 계산 (단위 벡터).
            // pitch 는 X축 회전 (아래로 내려다봄), yaw 는 Y축 회전 (수평 회전).
            // distance 는 매 프레임 적용하므로 여기서는 제외.
            Quaternion rot = Quaternion.Euler(m_pitchDeg, m_yawDeg, 0f);
            // forward 가 "보는 방향" 이니, 카메라는 캐릭터에서 -forward 방향으로 떨어져 있어야 함.
            m_cameraOffsetDir = -(rot * Vector3.forward);

            // distance 초기값 설정. 인스펙터에서 설정한 m_distance 를 그대로 사용.
            // (범위를 벗어나면 안전하게 클램프)
            m_targetDistance = Mathf.Clamp(m_distance, m_minDistance, m_maxDistance);
            m_currentDistance = m_targetDistance;

            // 2. 카메라 회전은 고정. 위의 rot 가 그대로 카메라의 회전.
            transform.rotation = rot;

            // 3. 시작 시점에 LocalPlayer 가 이미 있으면 즉시 위치 스냅 (부드러운 추적 없이).
            // 안 그러면 카메라가 원점에서 캐릭터까지 천천히 끌려오는 부자연스러움이 생김.
            PlayerCharacter target = getTarget();
            if (target != null)
            {
                transform.position = target.transform.position + m_lookOffset + m_cameraOffsetDir * m_currentDistance;
            }
        }

        private void LateUpdate()
        {
            // LateUpdate 에서 카메라를 갱신하는 이유:
            // - PlayerCharacter.Update() 가 캐릭터 위치를 먼저 갱신함 (Update 단계)
            // - 그 후 LateUpdate 에서 카메라를 갱신해야 "이번 프레임의 캐릭터 위치" 를 정확히 따라감
            // - Update 에서 하면 카메라가 1 프레임 뒤처질 수 있음 (덜덜거림의 원인)

            // 1. 휠 입력으로 목표 distance 갱신.
            processZoomInput();

            // 2. 실제 distance 가 목표를 따라가도록 부드럽게 보간.
            m_currentDistance = Mathf.SmoothDamp(
                m_currentDistance,
                m_targetDistance,
                ref m_distanceVelocity,
                m_zoomSmoothTime);

            // 3. 캐릭터 따라가기.
            PlayerCharacter target = getTarget();
            if (target == null)
                return;

            Vector3 desiredPos = target.transform.position + m_lookOffset + m_cameraOffsetDir * m_currentDistance;

            // SmoothDamp: 목표 위치까지 부드럽게 보간.
            transform.position = Vector3.SmoothDamp(
                transform.position,
                desiredPos,
                ref m_velocity,
                m_smoothTime);
        }

        // 휠 델타를 읽어 목표 distance 를 갱신.
        // 휠을 앞으로 굴리면 (scrollY 양수) 줄인 (distance 감소),
        // 뒤로 굴리면 (scrollY 음수) 줄아웃 (distance 증가).
        private void processZoomInput()
        {
            if (Managers.Managers.Instance == null)
                return;

            float scrollY = Managers.Managers.Input.MouseScrollDelta.y;
            if (scrollY == 0f)
                return;

            // 휠 델타는 마우스마다 다른 단위로 들어올 수 있으므로 (보통 ±120),
            // 부호만 사용해서 고정된 m_zoomStep 을 적용.
            // 이렇게 하면 "휠 한 칸당 정확히 2m" 이 보장됨.
            float delta = scrollY > 0f ? -m_zoomStep : m_zoomStep;
            m_targetDistance = Mathf.Clamp(m_targetDistance + delta, m_minDistance, m_maxDistance);
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
