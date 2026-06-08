using System.Collections;
using System.Collections.Generic;
using MMO.Client.Navigation;
using UnityEngine;

namespace Client.Game
{
    // 1명의 캐릭터(내 캐릭터 또는 다른 유저)를 표현하는 컴포넌트.
    // LocalPlayer 는 StageManager.EnsureLocalPlayer 가 1회 생성해 영속 보관(스테이지 이동 시 숨김/재배치).
    // 타 유저는 ObjectVisibilityNtf 수신 시 동적으로 생성/제거한다.
    // ActorObject 를 상속하여 스탯(StatHolder)과 현재 HP/MP 를 갖는다.
    public class PlayerCharacter : ActorObject
    {
        // 캐릭터 식별자
        public long UserId { get; private set; }
        public string CharacterName { get; private set; }

        // 내 캐릭터 여부 (다른 유저와 구분하기 위해)
        public bool IsLocalPlayer { get; private set; }

        // 이동 관련
        [SerializeField] private bool m_useMoveSpeedOverride = false;   // (에디터 전용)이동속도를 오버라이드 할건지 여부
        [SerializeField] private float m_moveSpeedOverride = 5f;        // (에디터 전용)이동속도를 오버라이드할 값
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

        // ── 스킬 강제이동 (대시/블링크) ──
        // 일반 이동(m_hasMoveDest)과 별개. m_skillMoving 동안 updateMovement 가 ease-out 으로 전진.
        // 서버 Character::ApplySkillMovement 와 동일한 공식(f=1-(1-t)^2)을 사용한다.
        private bool m_skillMoving;
        private Vector3 m_skillMoveStart;
        private Vector3 m_skillMoveDir;        // 정규화 평면 방향
        private float m_skillMoveDistance;
        private float m_skillMoveDuration;
        private float m_skillMoveElapsed;

        public bool IsSkillMoving => m_skillMoving;

        // ─── Animator ────────────────────────────────────────────────
        // Visual 하위에 부착된 Animator. Awake 에서 찾아둠.
        // prefab 에 모델이 없거나 Animator 가 없으면 null 일 수 있음 (이 경우 애니메이션 없이 동작).
        private Animator m_animator;
        // Animator 파라미터 hash. 문자열 lookup 을 피해 성능 약간 좋음.
        private static readonly int s_paramSpeed = Animator.StringToHash("Speed");
        // Speed 파라미터 보간 시간(초). 이 시간 동안 idle↔walk↔run 블렌드를 거친다.
        private const float k_speedDampTime = 0.12f;
        // 시전 애니 재생속도 멀티플라이어. cast 스테이트의 Speed 가 이 파라미터를 Multiplier 로 써야 적용됨.
        private static readonly int s_paramCastSpeed = Animator.StringToHash("CastSpeed");
        // 이동시전(Mobile)용 상반신 오버라이드 레이어 이름/트리거. (베이스 Cast 트리거와 분리)
        private const string k_upperBodyLayerName = "UpperBody";
        private const string k_castUpperTrigger = "CastUpper";
        private int m_upperBodyLayer = -2;   // -2 = 미해결, -1 = 레이어 없음
        private Coroutine m_upperFade;
        // 이동 잠금 만료 시각(Time.time). Stationary 시전 동안 이동 입력을 막는다.
        private float m_moveLockedUntil;

        private void Awake()
        {
            // Visual 하위 어딘가에 있는 Animator 를 찾음. prefab 구조상 PlayerCharacter > Visual > Kiki-v2 에 부착되어 있음.
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
            {
                Debug.LogWarning($"[PlayerCharacter] Animator 를 찾지 못했습니다. 애니메이션 없이 동작합니다.");
            }
        }

        // 스킬 시전 애니메이션 1회 재생. triggerName = Animator 의 Trigger 파라미터 이름(Skill 데이터 CastAnim).
        // 빈 문자열이거나 Animator 가 없으면 무시한다.
        // castSpeed(= 시전클립 기본길이 / ActionLock초)를 받아 CastSpeed 파라미터에 넣어
        // 시전 애니가 ActionLock 길이에 맞게 재생되도록 한다.
        public void PlayCastAnimation(string triggerName, float castSpeed)
        {
            if (m_animator == null || string.IsNullOrEmpty(triggerName))
                return;

            // CastSpeed 를 먼저 세팅한 뒤 트리거 → cast 스테이트가 첫 프레임부터 맞는 속도로 재생.
            m_animator.SetFloat(s_paramCastSpeed, Mathf.Max(0.01f, castSpeed));
            m_animator.SetTrigger(triggerName);
        }

        // 이동시전(Mobile): 상반신 레이어 가중치를 올리고 상반신 시전 트리거를 쏘다.
        // 베이스 레이어는 locomotion 을 유지하므로 다리는 계속 달린다. durationSec 뒤 가중치를 0 으로 페이드.
        public void PlayCastUpperBody(float castSpeed, float durationSec)
        {
            if (m_animator == null)
                return;

            m_animator.SetFloat(s_paramCastSpeed, Mathf.Max(0.01f, castSpeed));

            int layer = resolveUpperBodyLayer();
            if (layer < 0)
                return;   // 상반신 레이어 없음 → 미구성. (이동시전 스킬엔 UpperBody 레이어가 필요)

            m_animator.SetLayerWeight(layer, 1f);
            m_animator.SetTrigger(k_castUpperTrigger);

            if (m_upperFade != null)
                StopCoroutine(m_upperFade);
            m_upperFade = StartCoroutine(fadeUpperBodyOut(layer, durationSec));
        }

        private int resolveUpperBodyLayer()
        {
            if (m_upperBodyLayer == -2 && m_animator != null)
                m_upperBodyLayer = m_animator.GetLayerIndex(k_upperBodyLayerName);
            return m_upperBodyLayer;
        }

        // durationSec 동안 유지 후 상반신 레이어 가중치를 0 으로 부드럽게 내린다.
        private IEnumerator fadeUpperBodyOut(int layer, float holdSec)
        {
            if (holdSec > 0f)
                yield return new WaitForSeconds(holdSec);

            const float fadeSec = 0.15f;
            float start = m_animator.GetLayerWeight(layer);
            float t = 0f;
            while (t < fadeSec)
            {
                t += Time.deltaTime;
                m_animator.SetLayerWeight(layer, Mathf.Lerp(start, 0f, t / fadeSec));
                yield return null;
            }
            m_animator.SetLayerWeight(layer, 0f);
            m_upperFade = null;
        }

        // 이동을 seconds 동안 잠그고(Stationary 시전), 현재 이동도 즉시 멈춘다.
        public void LockMovement(float seconds)
        {
            m_moveLockedUntil = Time.time + seconds;
            StopMove();
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
            m_skillMoving = false; // 스킬 강제이동도 취소
            m_path.Clear();
            m_pathIndex = 0;
        }

        // ─── 이동 ───────────────────────────────────────────────────────

        // 플레이어캐릭터 이동속도 얻기 함수
        public float GetMoveSpeed()
        {
            if (m_useMoveSpeedOverride)
            {
                return m_moveSpeedOverride;
            }
            else
            {
                return (float)Stats.Get(GameData.EStat.MoveSpdTotal);
            }
        }

        // 목적지 설정. 마우스 누르고 있는 동안 매 프레임 갱신됨.
        // - 입력된 dest 는 NavMesh 위로 보정한다 (마우스가 절벽 아래 등을 가리킬 수도 있어서).
        // - NavMesh 가 로드되지 않은 경우 직선 이동으로 폴백.
        // - 경로 재계산은 200ms 마다 (또는 목적지가 의미 있게 바뀌었을 때).
        public void SetMoveDestination(Vector3 dest)
        {
            if (m_skillMoving)
                return;   // 스킬 강제이동(대시/블링크) 중에는 일반 이동 입력 무시.
            if (Time.time < m_moveLockedUntil)
                return;   // 시전 중 이동 잠금 (Stationary 캐스트).
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

        // 스킬 강제이동 시작 (이동기/순간이동).
        //   durationSec <= 0 : 즉시 블링크(끝점 스냅) — 순간이동.
        //   durationSec >  0 : duration 동안 ease-out 감속 대시 — 글라이드.
        // 서버 Character::ApplySkillMovement 와 동일한 공식이어야 위치가 동기화된다.
        // 지형 충돌은 v1 무시 (완전 결정론).
        public void StartSkillMove(Vector3 dirFlat, float distance, float durationSec)
        {
            Vector3 d = new Vector3(dirFlat.x, 0f, dirFlat.z);
            if (d.sqrMagnitude < 1e-6f || distance <= 0f)
                return;
            d.Normalize();

            // 일반 이동 중단 + 시전 방향으로 즉시 회전.
            StopMove();
            transform.rotation = Quaternion.LookRotation(d);

            if (durationSec <= 0f)
            {
                // 즉시 블링크: 끝점 스냅 (Y 유지).
                Vector3 end = transform.position + d * distance;
                end.y = transform.position.y;
                transform.position = end;
                m_skillMoving = false;
                return;
            }

            m_skillMoveStart = transform.position;
            m_skillMoveDir = d;
            m_skillMoveDistance = distance;
            m_skillMoveDuration = durationSec;
            m_skillMoveElapsed = 0f;
            m_skillMoving = true;
        }

        // 강제이동 1 프레임 전진. ease-out(f=1-(1-t)^2), 종료 시 끝점 스냅. (updateMovement 에서 호출)
        private void advanceSkillMove()
        {
            m_skillMoveElapsed += Time.deltaTime;
            float t = (m_skillMoveDuration > 0f) ? m_skillMoveElapsed / m_skillMoveDuration : 1f;

            if (t >= 1f)
            {
                Vector3 end = m_skillMoveStart + m_skillMoveDir * m_skillMoveDistance;
                end.y = m_skillMoveStart.y;
                transform.position = end;
                m_skillMoving = false;
                return;
            }

            float f = 1f - (1f - t) * (1f - t);
            Vector3 p = m_skillMoveStart + m_skillMoveDir * (m_skillMoveDistance * f);
            p.y = m_skillMoveStart.y;
            transform.position = p;
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

        // Animator 의 Speed(float) 파라미터를 이동 상태와 동기화.
        // 이동 여부를 0/1 목표값으로 두고 damping 으로 보간 → 가/감속 구간에서 walk 블렌드를
        // 거쳐 run 으로 자연스럽게 전환된다. (블렌드 트리: idle=0, walk=0.5, run=1)
        // SetFloat damping 은 매 프레임 호출해야 보간되므로 캐시 가드 없이 호출한다.
        private void updateAnimator()
        {
            if (m_animator == null) return;

            float target = m_hasMoveDest ? 1f : 0f;
            m_animator.SetFloat(s_paramSpeed, target, k_speedDampTime, Time.deltaTime);
        }

        private void updateMovement()
        {
            // 스킬 강제이동이 우선 (일반 이동과 배타적).
            if (m_skillMoving)
            {
                advanceSkillMove();
                return;
            }

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
            float moveSpeed = GetMoveSpeed();
            float step = moveSpeed * Time.deltaTime;

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
            float moveSpeed = GetMoveSpeed();
            float step = moveSpeed * Time.deltaTime;

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
