using Client.Network;
using Common;
using GamePacket;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Client.Game
{
    // 마우스 좌클릭으로 LocalPlayer를 이동시키고, 서버에도 알린다.
    //
    // 클라이언트.md 규칙:
    //   - 누르고 있는 동안: 매 프레임 클라 측 캐릭터 이동 + 500ms마다 서버에 MoveReq(목적지)
    //   - 뗀 순간: 서버에 MoveReq(현재 위치) 1회
    //
    // 서버 측은 현재 MoveReq를 받기만 하고 처리는 안 함 (Stage::OnUserPacket이 로그만 찍음).
    public class MouseInputHandler : MonoBehaviour
    {
        [SerializeField] private Camera m_camera;

        // raycast 최대 거리
        [SerializeField] private float m_rayMaxDistance = 100f;

        // 누르고 있는 동안 서버에 MoveReq 보내는 주기 (초)
        [SerializeField] private float m_sendIntervalSec = 0.5f;

        // 디버그 로그
        [SerializeField] private bool m_debugLog = false;

        private bool m_wasPressed = false;

        // 마지막으로 서버에 MoveReq 전송한 후 경과 시간 (sec)
        // 누르고 있는 첫 프레임에 즉시 보내기 위해 큰 값으로 초기화.
        private float m_timeSinceLastSend = float.MaxValue;

        // 마지막으로 보낸 목적지 (중복 송신 방지 — 같은 목적지면 안 보냄)
        private Vector3 m_lastSentDest;
        private bool m_hasLastSent = false;

        private void Awake()
        {
            if (m_camera == null)
            {
                m_camera = Camera.main;
            }

            // Unity 6.x에서 기본 Plane mesh의 normal이 -Y로 잡히는 케이스 대응.
            Physics.queriesHitBackfaces = true;
        }

        private void Update()
        {
            if (m_camera == null) return;
            if (Mouse.current == null) return;

            PlayerCharacter local = StageManager.Instance?.LocalPlayer;
            if (local == null) return;

            bool isPressed = Mouse.current.leftButton.isPressed;
            m_timeSinceLastSend += Time.deltaTime;

            if (isPressed)
            {
                Vector2 screenPos = Mouse.current.position.ReadValue();
                if (tryGetGroundPoint(screenPos, local, out Vector3 worldPoint))
                {
                    local.SetMoveDestination(worldPoint);

                    if (m_debugLog && !m_wasPressed)
                    {
                        Debug.Log($"[MouseInput] Move to {worldPoint}");
                    }

                    // 첫 클릭 프레임이거나 주기가 지났으면 서버에 전송
                    // 단, 목적지가 거의 동일하면 굳이 안 보냄 (대역폭 절약)
                    if (m_timeSinceLastSend >= m_sendIntervalSec || !m_wasPressed)
                    {
                        if (!m_hasLastSent || (worldPoint - m_lastSentDest).sqrMagnitude > 0.01f)
                        {
                            sendMoveDestReq(worldPoint, local.transform.position, local.transform.eulerAngles.y);
                            m_lastSentDest = worldPoint;
                            m_hasLastSent = true;
                            m_timeSinceLastSend = 0f;
                        }
                    }
                }
            }
            else
            {
                // 누르고 있다가 뗀 순간 (이번 프레임에 막 떼어짐)
                if (m_wasPressed)
                {
                    if (local.IsMoving)
                    {
                        local.StopMove();
                        if (m_debugLog) Debug.Log("[MouseInput] Stop");
                    }
                    // 떼는 순간 현재 캐릭터 위치를 서버에 알림
                    Vector3 curPos = local.transform.position;
                    sendMoveStopReq(curPos, local.transform.eulerAngles.y);
                    m_hasLastSent = false;
                    m_timeSinceLastSend = 0f;
                }
            }

            m_wasPressed = isPressed;
        }

        private void sendMoveDestReq(Vector3 dest, Vector3 curPos, float dirY)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;

            // 좌표계: Unity 와 동일 (X, Y, Z). 서버와 그대로 매핑.
            // dest = 도착할 목적지, curPos = 클라 현재 위치 (서버가 검증에 사용).
            MoveDestReq req = new MoveDestReq
            {
                DestX = dest.x,
                DestY = dest.y,
                DestZ = dest.z,
                PosX  = curPos.x,
                PosY  = curPos.y,
                PosZ  = curPos.z,
            };
            net.Send(GamePacketId.MoveDestReq, req);

            if (m_debugLog)
            {
                Debug.Log($"[MouseInput] Sent MoveDestReq dest=({dest.x:F2},{dest.y:F2},{dest.z:F2}) pos=({curPos.x:F2},{curPos.y:F2},{curPos.z:F2}) dirY={dirY:F1}");
            }
        }

        private void sendMoveStopReq(Vector3 pos, float dirY)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;

            MoveStopReq req = new MoveStopReq
            {
                PosX = pos.x,
                PosY = pos.y,
                PosZ = pos.z,
                Yaw = dirY,
            };
            // 이전 코드 버그: MoveStopReq인데 GamePacketId.MoveDestReq로 보냄. 수정.
            net.Send(GamePacketId.MoveStopReq, req);

            if (m_debugLog)
            {
                Debug.Log($"[MouseInput] Sent MoveStopReq pos=({pos.x:F2},{pos.y:F2},{pos.z:F2}) dirY={dirY:F1}");
            }
        }


        // 마우스 화면좌표에서 ray를 쏴서 LocalPlayer 자신을 제외한 첫 충돌점을 구한다.
        //
        // 1단계: Physics.RaycastAll 로 콜라이더 충돌 찾기 (기존 동작).
        //        평면 메시 안쪽에 마우스가 있으면 정확한 ground 점 반환.
        // 2단계 (fallback): 콜라이더에 안 닿으면 (마우스가 평면 너머 허공),
        //        캐릭터 y 평면과 ray 의 수학적 교차점 계산.
        //        NavMesh 바깥점이 ClampToNavMesh 로 전달되어 가장자리까지 이동하게 함.
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
