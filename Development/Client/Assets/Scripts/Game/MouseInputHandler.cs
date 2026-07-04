using Client.Managers;
using Client.Network;
using Common;
using GamePacket;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;

namespace Client.Game
{
    // 마우스 입력을 "월드 좌표 + 이동 의도" 로 해석하는 컴포넌트.
    //
    // 단일 책임: 화면 좌표 → 월드 좌표 변환.
    //   - InputManager 로부터 마우스 상태(눌림 여부, 화면 좌표)를 받음
    //   - raycast + 평면 폴백으로 월드 좌표 계산
    //   - LocalPlayer 의 PlayerMoveController 에게 "이 좌표로 가고 싶다" 라고 전달
    //
    // 책임이 아닌 것:
    //   - 저수준 입력 읽기 (InputManager 가 함)
    //   - 캐릭터 이동 시뮬레이션 (PlayerCharacter 가 함)
    //   - 서버 송신 (PlayerMoveController 가 함)
    //   - 송신 타이밍 (PlayerMoveController 가 함)
    //
    // 씬에 하나만 존재. GameInputBridge 같은 빈 GameObject 에 붙여 사용.
    public class MouseInputHandler : MonoBehaviour
    {
        [SerializeField] private Camera m_camera;

        // raycast 최대 거리
        [SerializeField] private float m_rayMaxDistance = 100f;

        // 디버그 로그
        [SerializeField] private bool m_debugLog = false;

        // 씬에 하나만 존재. 스킬 시스템 등이 마우스 지면점을 조회할 때 사용.
        public static MouseInputHandler Instance { get; private set; }

        private void Awake()
        {
            Instance = this;

            if (m_camera == null)
            {
                m_camera = Camera.main;
            }

            // Unity 6.x 에서 기본 Plane mesh 의 normal 이 -Y 로 잡히는 케이스 대응.
            Physics.queriesHitBackfaces = true;
        }

        private void OnDestroy()
        {
            if (Instance == this)
                Instance = null;
        }

        // 현재 마우스 화면좌표 아래의 월드 지면점을 구한다 (스킬 조준용). 클릭 상태와 무관.
        // selfToIgnore: 레이캐스트에서 제외할 캐릭터(보통 LocalPlayer). 실패 시 false.
        public bool TryGetMouseGroundPoint(PlayerCharacter selfToIgnore, out Vector3 worldPoint)
        {
            worldPoint = Vector3.zero;
            if (m_camera == null)
                return false;

            InputManager input = Managers.Managers.Input;
            if (input == null)
                return false;

            return tryGetGroundPoint(input.MouseScreenPosition, selfToIgnore, out worldPoint);
        }

        private void Update()
        {
            if (m_camera == null) return;

            InputManager input = Managers.Managers.Input;
            if (input == null) return;

            // 마우스 좌클릭이 눌려있지 않으면 할 일 없음.
            // (송신이나 정지 처리는 PlayerMoveController 가 자기 상태로 알아서 함.)
            if (!input.IsMouseClickHeld) return;

            // UI(감정표현 패널 등) 위를 클릭 중이면 월드 이동을 하지 않는다.
            // UI 버튼/딤 배경이 raycastTarget 이면 EventSystem 이 이를 감지한다.
            if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
                return;

            PlayerMoveController mover = findLocalMoveController();
            if (mover == null) return;

            Vector2 screenPos = input.MouseScreenPosition;
            if (!tryGetGroundPoint(screenPos, mover.Character, out Vector3 worldPoint))
                return;

            mover.RequestMoveTo(worldPoint);

            if (m_debugLog) Debug.Log($"[MouseInput] requested move to {worldPoint}");
        }

        // LocalPlayer 의 PlayerMoveController 를 찾는다.
        // 정적 접근을 매 프레임 하지만, GetComponent 1회 호출이라 비용은 미미.
        // LocalPlayer 가 바뀌어도 (스테이지 이동 등) 자동으로 새 mover 를 따라감.
        private PlayerMoveController findLocalMoveController()
        {
            PlayerCharacter local = StageManager.Instance?.LocalPlayer;
            if (local == null) return null;
            return local.GetComponent<PlayerMoveController>();
        }

        // 마우스 화면좌표에서 ray 를 쏴서 LocalPlayer 자신을 제외한 첫 충돌점을 구한다.
        //
        // 1단계: Physics.RaycastAll 로 콜라이더 충돌 찾기.
        //        평면 메시 안쪽에 마우스가 있으면 정확한 ground 점 반환.
        // 2단계 (fallback): 콜라이더에 안 닿으면 (마우스가 평면 너머 허공),
        //        캐릭터 y 평면과 ray 의 수학적 교차점 계산.
        //        NavMesh 바깥점은 PlayerCharacter.SetMoveDestination 안의
        //        ClampToNavMesh 가 가장자리까지 보정해 줌.
        //
        // 이 fallback 이 없으면 마우스가 콜라이더 바깥일 때 SetMoveDestination 자체가 호출 안 되어
        // 캐릭터가 입력에 반응하지 않게 된다 (클릭 무브 게임에서 매우 부자연스러움).
        private bool tryGetGroundPoint(Vector2 screenPos, PlayerCharacter selfToIgnore, out Vector3 worldPoint)
        {
            worldPoint = Vector3.zero;
            Ray ray = m_camera.ScreenPointToRay(screenPos);

            // 1단계: 콜라이더 hit 찾기
            RaycastHit[] hits = Physics.RaycastAll(ray, m_rayMaxDistance);
            float bestDist = float.MaxValue;
            bool found = false;

            for (int i = 0; i < hits.Length; ++i)
            {
                RaycastHit h = hits[i];
                if (selfToIgnore != null && h.collider.transform.IsChildOf(selfToIgnore.transform))
                    continue;

                // 몬스터 콜라이더는 지면 클릭 대상에서 제외. (투사체 hit 판정용으로 추가된
                // 몬스터 콜라이더가 이동 클릭 좌표를 가로채지 않도록. 몬스터를 클릭해도 지면/그 너머로 이동 좌표가 잡힌다.)
                if (h.collider.GetComponentInParent<MonsterObject>() != null)
                    continue;

                if (h.distance < bestDist)
                {
                    bestDist = h.distance;
                    worldPoint = h.point;
                    found = true;
                }
            }

            if (found) return true;

            // 2단계 fallback: 캐릭터 y 의 무한 평면과 ray 의 교차점.
            // 평면 방정식: (P - planePoint) · normal = 0, normal = (0,1,0).
            // ray: P(t) = ray.origin + t * ray.direction
            // 풀면: t = (planeY - ray.origin.y) / ray.direction.y
            float planeY = (selfToIgnore != null) ? selfToIgnore.transform.position.y : 0f;
            float denom = ray.direction.y;

            // 카메라가 거의 수평이면 ray.direction.y 가 0 근처라 교차점이 무한대로 멀어짐.
            // 쿼터뷰에서는 거의 발생 안 하지만 안전 가드.
            if (Mathf.Abs(denom) < 1e-4f) return false;

            float t = (planeY - ray.origin.y) / denom;
            // ray 가 평면 "뒤" 방향이면 (카메라 위쪽 하늘 클릭 같은 경우) 음수 t. 무시.
            if (t < 0f) return false;

            worldPoint = ray.origin + ray.direction * t;
            return true;
        }
    }
}
