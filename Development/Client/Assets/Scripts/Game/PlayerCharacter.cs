using System.Collections.Generic;
using MMO.Client.Navigation;
using UnityEngine;

namespace Client.Game
{
    // 1명의 캐릭터(내 캐릭터 또는 다른 유저)를 표현하는 컴포넌트.
    // StageManager가 GameEnterNtf/StageEnterNtf/CharacterEnterNtf 수신 시 동적으로 생성한다.
    public class PlayerCharacter : MonoBehaviour
    {
        // 캐릭터 식별자
        public long UserId { get; private set; }
        public string CharacterName { get; private set; }

        // 내 캐릭터 여부 (다른 유저와 구분하기 위해)
        public bool IsLocalPlayer { get; private set; }

        // 이동 관련
        [SerializeField] private float m_moveSpeed = 5f;             // m/s
        [SerializeField] private float m_rotateSpeedDeg = 720f;      // 초당 회전 각도 (deg/sec). 720 = 0.5초에 한바퀴.
        private const float k_arriveThreshold = 0.05f;               // 최종 목적지 도착 판정 거리
        private const float k_waypointThreshold = 0.25f;             // 중간 waypoint 도달 판정 거리
        private const float k_repathIntervalSec = 0.2f;              // NavMesh 경로 재계산 최소 간격
        private const float k_repathMinMoveSqr = 0.25f;              // 목적지가 이 거리(0.5m) 이상 바뀌어야 재계산 (sqr)
        private const float k_sampleRadius = 2.0f;                   // NavMesh 위로 위치 보정할 때 검색 반경

        private bool m_hasMoveDest = false;
        private Vector3 m_moveDest;

        // 현재 따라가는 경로 (waypoint 리스트).
        // pathIndex 가 현재 향하고 있는 waypoint 의 인덱스.
        private readonly List<Vector3> m_path = new List<Vector3>(32);
        private int m_pathIndex;

        // 마지막으로 FindPath 를 호출한 시점 (Time.time)
        private float m_lastPathTime = -999f;
        // 마지막으로 FindPath 에 사용한 목적지 (이거랑 거의 같으면 재계산 안 함)
        private Vector3 m_lastPathDest;

        // 캐릭터가 현재 이동 중인지
        public bool IsMoving => m_hasMoveDest;

        // ─── Animator ────────────────────────────────────────────────
        // Visual 하위에 부착된 Animator. Awake 에서 찾아둠.
        // prefab 에 모델이 없거나 Animator 가 없으면 null 일 수 있음 (이 경우 애니메이션 없이 동작).
        private Animator m_animator;
        // Animator 에 마지막으로 보낸 IsMoving 값. 매 프레임 SetBool 호출을 피하기 위한 캐시.
        private bool m_lastSentIsMoving;
        // Animator 파라미터 hash. 문자열 lookup 을 피해 성능 약간 좋음.
        private static readonly int s_paramIsMoving = Animator.StringToHash("IsMoving");

        private void Awake()
        {
            // Visual 하위 어딘가에 있는 Animator 를 찾음. prefab 구조상 PlayerCharacter > Visual > Kiki-v2 에 부착되어 있음.
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
            {
                Debug.LogWarning($"[PlayerCharacter] Animator 를 찾지 못했습니다. 애니메이션 없이 동작합니다.");
            }
        }

        // StageManager가 캐릭터 생성 직후 1회 호출
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

            // (LocalPlayer/타 유저 시각 구분은 추후 닉네임/머리위 UI 로 처리)
        }

        // 서버에서 위치 갱신 패킷이 왔을 때 호출
        // 일단은 보간 없이 즉시 텔레포트. 보간은 나중에.
        public void SetPosition(Vector3 pos, float dirY)
        {
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
            m_hasMoveDest = false; // 위치 강제 동기화는 이동 취소
            m_path.Clear();
            m_pathIndex = 0;
        }

        // ─── 이동 ───────────────────────────────────────────────────────

        // 목적지 설정. 마우스 누르고 있는 동안 매 프레임 갱신됨.
        // - 입력된 dest 는 NavMesh 위로 보정한다 (마우스가 절벽 아래 등을 가리킬 수도 있어서).
        // - NavMesh 가 로드되지 않은 경우 직선 이동으로 폴백.
        // - 경로 재계산은 200ms 마다 (또는 목적지가 의미 있게 바뀌었을 때).
        public void SetMoveDestination(Vector3 dest)
        {
            // dest 의 y 는 그대로 둡다. NavMesh 검색 시 자동으로 표면 Y 로 보정됨.
            // (이전에는 transform.position.y 로 덮어써서 지형 높낮이가 반영되지 않았음)

            if (!NavMeshService.IsLoaded)
            {
                // NavMesh 없음 -> 직선 이동 폴백.
                // 한 번만 경고 (스팸 방지를 위해 m_hasMoveDest 가 false 일 때만).
                if (!m_hasMoveDest)
                    Debug.LogWarning("[PlayerCharacter] NavMesh 가 로드되지 않았습니다. 직선 이동으로 폴백합니다.");

                m_moveDest = dest;
                m_hasMoveDest = true;
                m_path.Clear();
                m_pathIndex = 0;
                return;
            }

            // 마우스가 NavMesh 바깥을 가리키면 "캐릭터가 갈 수 있는 가장 먼 점"으로 클램프.
            // 이렇게 하면 마우스를 어디에 놓든 캐릭터가 그 방향으로 갈 수 있는 만큼 이동한다 (클릭 무브 게임의 표준 방식).
            // destOnNav 에는 NavMesh 표면의 정확한 Y 값이 들어있으므로 그대로 사용.
            if (!NavMeshService.ClampToNavMesh(transform.position, dest, out Vector3 destOnNav))
            {
                // 클램프도 실패 (캐릭터가 NavMesh 위에 없는 드문 상황).
                // 현재 이동 상태 유지하고 이번 입력은 무시.
                return;
            }

            m_moveDest = destOnNav;
            m_hasMoveDest = true;

            // 경로 재계산 여부 판정.
            // - 처음 호출이거나 (m_path 비어있음)
            // - 마지막 재계산으로부터 200ms 이상 경과
            // - 마지막 재계산 시 사용한 목적지와 의미 있게 다름 (0.5m 이상)
            bool needRepath =
                m_path.Count == 0 ||
                (Time.time - m_lastPathTime) >= k_repathIntervalSec ||
                (destOnNav - m_lastPathDest).sqrMagnitude >= k_repathMinMoveSqr;

            if (needRepath)
                recalculatePath(destOnNav);
        }

        // 즉시 멈춤. (마우스 버튼을 뗐을 때)
        public void StopMove()
        {
            m_hasMoveDest = false;
            m_path.Clear();
            m_pathIndex = 0;
        }

        // NavMesh 경로 재계산. 시작점은 현재 캐릭터 위치 (NavMesh 위로 보정해서).
        private void recalculatePath(Vector3 destOnNav)
        {
            // 시작점도 NavMesh 위로 보정 (캐릭터가 살짝 떠 있거나 가라앉아도 안전).
            Vector3 startPos = transform.position;
            if (NavMeshService.SamplePosition(startPos, k_sampleRadius, out Vector3 startOnNav))
                startPos = startOnNav;

            bool ok = NavMeshService.FindPath(startPos, destOnNav, m_path);
            m_pathIndex = 0;
            m_lastPathTime = Time.time;
            m_lastPathDest = destOnNav;

            if (!ok)
            {
                // 경로 못 찾음. 이동 중단.
                m_path.Clear();
            }
        }

        private void Update()
        {
            // 이동 로직은 m_hasMoveDest 가 true 일 때만 돌고,
            // Animator 갱신은 정지 전환(true -> false)도 잡아야 하므로 매 프레임 호출.
            updateMovement();
            updateAnimator();
        }

        // Animator 의 IsMoving 파라미터를 m_hasMoveDest 와 동기화.
        // 값이 바뀐 시점에만 SetBool 호출 (불필요한 Animator 갱신 방지).
        private void updateAnimator()
        {
            if (m_animator == null) return;

            if (m_lastSentIsMoving != m_hasMoveDest)
            {
                m_animator.SetBool(s_paramIsMoving, m_hasMoveDest);
                m_lastSentIsMoving = m_hasMoveDest;
            }
        }

        private void updateMovement()
        {
            if (!m_hasMoveDest)
                return;

            // NavMesh 폴백 (직선 이동): m_path 가 비어있고 m_moveDest 만 있는 경우.
            // NavMesh 사용 시에는 항상 m_path 를 따라가므로 이쪽 분기로 안 옴.
            if (m_path.Count == 0)
            {
                moveStraightTo(m_moveDest, finalDest: true);
                return;
            }

            // NavMesh waypoint 따라가기:
            // - 현재 인덱스의 waypoint 까지 직선 이동
            // - 도달하면 다음 waypoint 로 인덱스 증가
            // - 마지막 waypoint 도달하면 정지

            // 안전 가드: 이전 호출 후 다른 코드가 path 를 비웠을 수 있음.
            if (m_pathIndex >= m_path.Count)
            {
                m_hasMoveDest = false;
                m_path.Clear();
                m_pathIndex = 0;
                return;
            }

            bool isLastWaypoint = (m_pathIndex == m_path.Count - 1);
            Vector3 target = m_path[m_pathIndex];
            // target.y 는 NavMesh 에서 채워준 지형 표면 Y 이므로 그대로 사용.
            // (이전에는 transform.position.y 로 덮어써서 지형이 고르지 않아 캐릭터가 파뭐혀버렸음)

            float threshold = isLastWaypoint ? k_arriveThreshold : k_waypointThreshold;

            Vector3 cur = transform.position;
            Vector3 diff = target - cur;
            float dist = diff.magnitude;

            if (dist < threshold)
            {
                // waypoint 도달.
                if (isLastWaypoint)
                {
                    transform.position = target;
                    m_hasMoveDest = false;
                    m_path.Clear();
                    m_pathIndex = 0;
                    return;
                }

                // 중간 waypoint: 다음으로 진행. 진행 후에도 이번 프레임 step 을 다 못 쓴 셈인데,
                // 단순성을 위해 다음 프레임으로 미룬다 (waypoint 간격이 통상 step 보다 충분히 큼).
                m_pathIndex++;
                return;
            }

            Vector3 dir = diff / dist;
            float step = m_moveSpeed * Time.deltaTime;

            if (step >= dist)
            {
                // 이번 프레임에 waypoint 를 지나칠 거면 정확히 waypoint 에 도착시키고
                // 다음 프레임에서 다음 waypoint 로 이어가게 함.
                transform.position = target;
                if (isLastWaypoint)
                {
                    m_hasMoveDest = false;
                    m_path.Clear();
                    m_pathIndex = 0;
                }
                else
                {
                    m_pathIndex++;
                }
            }
            else
            {
                transform.position = cur + dir * step;
            }

            // 진행 방향을 바라보기 (수평면에서만 회전, y축 회전만). 부드럽게 보간.
            rotateTowardsDirection(dir);
        }

        // 주어진 방향(dir, 수평면)으로 캐릭터를 부드럽게 회전시킨다.
        // RotateTowards 는 "초당 N도" 일정한 각속도로 회전하므로 직관적이고 예측 가능.
        // (Slerp 는 멀수록 빨라지고 가까울수록 느려져서 "빙글빙글 도는 동안 회전이 느려지는" 식의 미묘한 어색함이 생김)
        private void rotateTowardsDirection(Vector3 dir)
        {
            // 수평면 성분만 계산 (Y 제외).
            // Y 가 포함된 dir 을 그대로 LookRotation 에 넘기면 경사를 오르고 내릴 때 캐릭터가 앞으로 숨지거나 뒤로 제치지는 어색한 회전이 생김.
            // 그리고 dir 이 거의 수직(Y가 거의 ±1)이면 X/Z 가 0이라 LookRotation 경고 발생. 그래서 수평 벡터를 먼저 계산해서 가드한다.
            Vector3 flat = new Vector3(dir.x, 0f, dir.z);
            if (flat.sqrMagnitude < 0.0001f)
                return;

            Quaternion targetRot = Quaternion.LookRotation(flat);
            transform.rotation = Quaternion.RotateTowards(
                transform.rotation,
                targetRot,
                m_rotateSpeedDeg * Time.deltaTime);
        }

        // NavMesh 없을 때의 폴백 이동 (이전 동작과 동일).
        private void moveStraightTo(Vector3 target, bool finalDest)
        {
            Vector3 cur = transform.position;
            Vector3 diff = target - cur;
            float dist = diff.magnitude;

            if (dist < k_arriveThreshold)
            {
                transform.position = target;
                if (finalDest) m_hasMoveDest = false;
                return;
            }

            Vector3 dir = diff / dist;
            float step = m_moveSpeed * Time.deltaTime;

            if (step >= dist)
            {
                transform.position = target;
                if (finalDest) m_hasMoveDest = false;
            }
            else
            {
                transform.position = cur + dir * step;
            }

            rotateTowardsDirection(dir);
        }
    }
}
