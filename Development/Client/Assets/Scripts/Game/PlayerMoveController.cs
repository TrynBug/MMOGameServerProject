using Client.Managers;
using Client.Network;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 내 캐릭터의 이동 의도를 처리하는 컴포넌트.
    //
    // 책임:
    //   1) MouseInputHandler 가 호출하는 RequestMoveTo(worldPos) 를 받아서
    //      - PlayerCharacter.SetMoveDestination 호출 (즉시 클라 이동 시작/예측)
    //      - ~100ms 주기로 MoveIntentReq(MOVE_TO, dest) 를 서버에 송신 (위치 미포함, input_seq 부여)
    //   2) 마우스 클릭을 뗀 순간을 InputManager.MouseClickedUp 으로 감지해서
    //      - PlayerCharacter.StopMove 호출
    //      - MoveIntentReq(STOP) 1회 송신
    //
    // 부착:
    //   LocalPlayer 의 GameObject 에 PlayerCharacter 와 함께 붙는다.
    //   다른 캐릭터(타 유저)에는 절대 붙지 않는다 — 구조적으로 보장.
    //
    // 책임이 아닌 것:
    //   - 마우스 좌표 → 월드 좌표 변환 (MouseInputHandler 가 함)
    //   - waypoint 따라 이동하는 시뮬레이션 (PlayerCharacter 가 함)
    //   - 키 입력 감지 (InputManager 가 함)
    [RequireComponent(typeof(PlayerCharacter))]
    public class PlayerMoveController : MonoBehaviour
    {
        // 외부에서 자기 PlayerCharacter 에 접근할 일이 있을 때 (MouseInputHandler 등)
        public PlayerCharacter Character { get; private set; }

        // 서버에 MoveIntentReq(MOVE_TO) 보내는 주기 (초).
        // 스냅샷 스트리밍 모델에선 관찰자가 보는 권위 위치가 이 dest 로 구동되므로, 너무 성기면
        // 관찰자 화면에서 경로 어긋남/정지 시 snap 이 생긴다. 100ms(10Hz)로 신선도를 높인다.
        // (Phase 3 의도 입력 전환 시 MoveIntentReq 로 대체.)
        [SerializeField] private float m_sendIntervalSec = 0.1f;

        // 같은 목적지면 굳이 안 보냄 (대역폭 절약). 단위는 sqr distance.
        [SerializeField] private float m_minDestChangeSqr = 0.01f;

        [SerializeField] private bool m_debugLog = false;

        // 가장 마지막에 RequestMoveTo 로 받은 목적지 (이번 프레임에 보낼 후보)
        private Vector3 m_pendingDest;
        private bool m_hasPendingDest;

        // 가장 마지막에 서버로 송신한 목적지 (중복 송신 방지용)
        private Vector3 m_lastSentDest;
        private bool m_hasLastSent;

        // 마지막 송신 후 경과 시간 (sec). 큰 값으로 시작해서 첫 호출에 즉시 보내짐.
        private float m_timeSinceLastSend = float.MaxValue;

        // 단조 증가 입력 번호(화해용). 매 MoveIntentReq 마다 +1. 서버가 SnapshotNtf.ack_input_seq 로 되돌려준다.
        private uint m_inputSeq = 0;

        private void Awake()
        {
            Character = GetComponent<PlayerCharacter>();
        }

        private void OnEnable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
            {
                input.MouseClickedUp += onMouseClickedUp;
            }
        }

        private void OnDisable()
        {
            InputManager input = Managers.Managers.Input;
            if (input != null)
            {
                input.MouseClickedUp -= onMouseClickedUp;
            }
        }

        // MouseInputHandler 가 매 프레임 호출 (마우스를 누르고 있는 동안).
        public void RequestMoveTo(Vector3 worldPoint)
        {
            // 사망 중이면 예측 이동/서버 송신 모두 차단.
            if (Character.IsDead) return;

            // 1) 캐릭터에게 즉시 알림 (클라이언트 응답성)
            Character.SetMoveDestination(worldPoint);

            // 2) 송신 대상으로 저장. 실제 송신은 Update 에서 주기적으로.
            m_pendingDest = worldPoint;
            m_hasPendingDest = true;
        }

        private void Update()
        {
            m_timeSinceLastSend += Time.deltaTime;

            if (!m_hasPendingDest) return;

            // 송신 주기 도달 OR 아직 한 번도 안 보냈으면 보냄
            bool intervalReached = m_timeSinceLastSend >= m_sendIntervalSec;
            bool firstSend = !m_hasLastSent;
            if (!intervalReached && !firstSend) return;

            // 목적지가 의미 있게 바뀌었는지 확인 (대역폭 절약)
            if (m_hasLastSent && (m_pendingDest - m_lastSentDest).sqrMagnitude < m_minDestChangeSqr)
                return;

            sendMoveIntent(EMoveIntent.MoveIntentTo, m_pendingDest, transform.eulerAngles.y);
            m_lastSentDest = m_pendingDest;
            m_hasLastSent = true;
            m_timeSinceLastSend = 0f;
        }

        // 마우스 좌클릭을 뗀 순간 InputManager 가 호출.
        private void onMouseClickedUp()
        {
            if (!Character.IsMoving && !m_hasPendingDest) return;

            // 캐릭터 즉시 정지
            if (Character.IsMoving)
            {
                Character.StopMove();
            }

            // 서버에 정지 의도 전송 (위치는 안 보냄 — 서버 권위).
            sendMoveIntent(EMoveIntent.MoveIntentStop, transform.position, transform.eulerAngles.y);

            // 송신 상태 리셋
            m_hasPendingDest = false;
            m_hasLastSent = false;
            m_timeSinceLastSend = 0f;

            if (m_debugLog) Debug.Log("[PlayerMoveController] mouse up -> StopMove + MoveIntent(STOP)");
        }

        // 이동 의도 전송. 위치는 보내지 않는다(서버 권위). input_seq 를 붙여 서버가 화해 기준으로 쓰게 한다.
        // intent=STOP 일 때 dest 는 무시된다(서버가 자기 위치에서 정지).
        private void sendMoveIntent(EMoveIntent intent, Vector3 dest, float dirY)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;

            MoveIntentReq req = new MoveIntentReq
            {
                InputSeq     = ++m_inputSeq,
                ClientTimeMs = (uint)(Time.time * 1000f),
                Intent       = intent,
                DestX = dest.x,
                DestY = dest.y,
                DestZ = dest.z,
                Yaw   = dirY,
            };
            net.Send(GamePacketId.MoveIntentReq, req);

            // 화해(RTT 측정/게이팅)용으로 이 입력을 PlayerCharacter 에 기록.
            Character.NotifyInputSent(m_inputSeq, dest, intent == EMoveIntent.MoveIntentStop);

            if (m_debugLog)
            {
                Debug.Log($"[PlayerMoveController] MoveIntent {intent} seq={m_inputSeq} dest=({dest.x:F2},{dest.y:F2},{dest.z:F2}) dirY={dirY:F1}");
            }
        }
    }
}
