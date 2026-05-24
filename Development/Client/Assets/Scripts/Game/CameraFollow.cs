using UnityEngine;

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
        [SerializeField] private float m_pitchDeg = 50f;

        [Tooltip("수평 회전 각도 (도). 0 이면 정북향, 45 면 북동향. 트오세 느낌은 45 근처.")]
        [SerializeField] private float m_yawDeg = 45f;

        [Tooltip("캐릭터로부터의 거리 (m). 큐브 캐릭터 기준 10~15 가 적당.")]
        [SerializeField] private float m_distance = 12f;

        [Header("Follow")]
        [Tooltip("카메라의 시선이 향하는 점에 추가할 오프셋. 캐릭터의 머리 위쪽을 보고 싶으면 y 를 살짝 올린다.")]
        [SerializeField] private Vector3 m_lookOffset = new Vector3(0f, 1.0f, 0f);

        [Tooltip("부드러운 추적의 시간 상수 (초). 작을수록 단단하게 붙고, 크면 느리게 따라옴. 0.1~0.3 추천.")]
        [SerializeField] private float m_smoothTime = 0.15f;

        // 캐릭터로부터 카메라로의 방향 벡터 (yaw/pitch 로부터 계산). Start 에서 1회 계산.
        private Vector3 m_cameraOffset;

        // SmoothDamp 의 내부 상태 (속도). Unity 의 SmoothDamp 가 부드러운 추적을 위해 ref 로 받는 변수.
        private Vector3 m_velocity = Vector3.zero;

        private void Start()
        {
            // 1. pitch/yaw/distance 로부터 카메라 오프셋 계산.
            // pitch 는 X축 회전 (아래로 내려다봄), yaw 는 Y축 회전 (수평 회전).
            // 결과적으로 카메라가 캐릭터로부터 어느 방향으로 얼마나 떨어져 있을지를 결정.
            Quaternion rot = Quaternion.Euler(m_pitchDeg, m_yawDeg, 0f);
            // 카메라가 바라보는 방향의 반대로 distance 만큼 이동한 지점이 카메라 위치.
            // forward 가 "보는 방향" 이니, 카메라는 캐릭터에서 -forward * distance 만큼 떨어져 있어야 함.
            m_cameraOffset = -(rot * Vector3.forward) * m_distance;

            // 2. 카메라 회전은 고정. 위의 rot 가 그대로 카메라의 회전.
            transform.rotation = rot;

            // 3. 시작 시점에 LocalPlayer 가 이미 있으면 즉시 위치 스냅 (부드러운 추적 없이).
            // 안 그러면 카메라가 원점에서 캐릭터까지 천천히 끌려오는 부자연스러움이 생김.
            PlayerCharacter target = getTarget();
            if (target != null)
            {
                transform.position = target.transform.position + m_lookOffset + m_cameraOffset;
            }
        }

        private void LateUpdate()
        {
            // LateUpdate 에서 카메라를 갱신하는 이유:
            // - PlayerCharacter.Update() 가 캐릭터 위치를 먼저 갱신함 (Update 단계)
            // - 그 후 LateUpdate 에서 카메라를 갱신해야 "이번 프레임의 캐릭터 위치" 를 정확히 따라감
            // - Update 에서 하면 카메라가 1 프레임 뒤처질 수 있음 (덜덜거림의 원인)

            PlayerCharacter target = getTarget();
            if (target == null)
                return;

            Vector3 desiredPos = target.transform.position + m_lookOffset + m_cameraOffset;

            // SmoothDamp: 목표 위치까지 부드럽게 보간.
            // Lerp 와 달리 가속/감속이 자연스럽고, 프레임 레이트에 영향이 적음.
            transform.position = Vector3.SmoothDamp(
                transform.position,
                desiredPos,
                ref m_velocity,
                m_smoothTime);
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
