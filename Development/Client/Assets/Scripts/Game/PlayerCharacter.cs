using UnityEngine;

namespace Client.Game
{
    // 1명의 캐릭터(내 캐릭터 또는 다른 유저)를 표현하는 컴포넌트.
    // StageManager가 GameEnterNtf/StageEnterNtf/CharacterEnterNtf 수신 시 동적으로 생성한다.
    //
    // A-1: 위치 표시.
    // A-2: 선형보간 이동 (클라 측만, 서버 통신 없음).
    public class PlayerCharacter : MonoBehaviour
    {
        // 캐릭터 식별자
        public long UserId { get; private set; }
        public string CharacterName { get; private set; }

        // 내 캐릭터 여부 (다른 유저와 구분하기 위해)
        public bool IsLocalPlayer { get; private set; }

        // 이동 관련
        [SerializeField] private float m_moveSpeed = 5f;             // m/s
        private const float k_arriveThreshold = 0.05f;               // 이거리 이내면 도착 처리
        private bool m_hasMoveDest = false;
        private Vector3 m_moveDest;

        // 캐릭터가 현재 이동 중인지
        public bool IsMoving => m_hasMoveDest;

        // StageManager가 캡슐 생성 직후 1회 호출
        public void Initialize(long userId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            UserId = userId;
            CharacterName = name;
            IsLocalPlayer = isLocalPlayer;

            // 위치/방향 적용
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // GameObject 이름을 식별 가능하게
            gameObject.name = $"Player_{userId}_{name}{(isLocalPlayer ? "_LOCAL" : "")}";

            // 내 캐릭터와 타 캐릭터 색상 구분 (디버깅 편의)
            Renderer rend = GetComponent<Renderer>();
            if (rend != null)
            {
                // 기본 머티리얼을 인스턴스화해서 색만 바꿈
                rend.material.color = isLocalPlayer ? Color.green : Color.gray;
            }
        }

        // 서버에서 위치 갱신 패킷이 왔을 때 호출
        // 일단은 보간 없이 즉시 텔레포트. 보간은 A-5에서.
        public void SetPosition(Vector3 pos, float dirY)
        {
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
            m_hasMoveDest = false; // 위치 강제 동기화는 이동 취소
        }

        // ─── 이동 ───────────────────────────────────────────────────────

        // 목적지 설정. 누르고 있는 동안 매 프레임 갱신됨.
        // y는 무시하고 캐릭터의 현재 y를 유지 (지면 위에서만 움직임)
        public void SetMoveDestination(Vector3 dest)
        {
            dest.y = transform.position.y;
            m_moveDest = dest;
            m_hasMoveDest = true;
        }

        // 즉시 멈춤. (마우스 버튼을 뗐을 때)
        public void StopMove()
        {
            m_hasMoveDest = false;
        }

        private void Update()
        {
            if (!m_hasMoveDest)
                return;

            Vector3 cur = transform.position;
            Vector3 diff = m_moveDest - cur;
            float dist = diff.magnitude;

            if (dist < k_arriveThreshold)
            {
                transform.position = m_moveDest;
                m_hasMoveDest = false;
                return;
            }

            Vector3 dir = diff / dist;
            float step = m_moveSpeed * Time.deltaTime;

            // 이번 프레임에 목적지를 지나칠 거면 그냥 도착 처리
            if (step >= dist)
            {
                transform.position = m_moveDest;
                m_hasMoveDest = false;
            }
            else
            {
                transform.position = cur + dir * step;
            }

            // 진행 방향을 바라보기 (수평면에서만 회전, y축 회전만)
            float dirY = Mathf.Atan2(dir.x, dir.z) * Mathf.Rad2Deg;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
        }
    }
}
