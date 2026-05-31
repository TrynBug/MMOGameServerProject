using MMO.Client.Navigation;
using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 1마리 몬스터를 표현하는 컴포넌트.
    // 서버의 ObjectVisibilityNtf(monster_spawns) 수신 시 StageManager 가 동적으로 생성한다.
    //
    // ── 이동 (서버 권위) ──
    // 서버가 MoveNtf 로 목적지/정지를 보낸다. 다른 유저 캐릭터와 동일한 모델이다.
    //   - is_moving=true  : SetMoveDestination(dest) → 클라가 자기 NavMesh 로 경로를 만들어 따라감.
    //   - is_moving=false : SetPosition(pos, yaw)    → 서버 위치로 즉시 스냅.
    // 추종 로직은 PlayerCharacter 의 원격 캐릭터 처리와 동일하다. (향후 공통 추종 컴포넌트로 묶을 수 있음)
    //
    // 애니메이션은 IActorAnimator(AnimatorActorAnimator)를 통해 idle/move 를 재생한다(서버 MoveNtf 기반).
    // 스탯/HP 는 아직 없다. 필요해지면 ActorObject 상속 등으로 확장한다.
    // prefab 은 임의의 아트 에셋이라 이 컴포넌트가 미리 붙어있지 않을 수 있어 MonsterFactory 가 런타임에 AddComponent 한다.
    public class MonsterObject : MonoBehaviour
    {
        // 서버가 발급한 오브젝트 식별자 (디스폰 시 이 값으로 찾는다).
        public long ObjectId { get; private set; }

        // 몬스터 게임데이터 Key (종류 식별).
        public long MonsterKey { get; private set; }

        // 이동 속도(유닛/초). 서버 Monster 의 이동속도와 맞춰야 시각적으로 자연스럽다.
        // TODO(데이터): 서버/클라가 GameData_Monster 의 이동속도를 공유하거나 MoveNtf 에 실어 보내도록.
        [SerializeField] private float m_moveSpeed = 4f;
        [SerializeField] private float m_rotateSpeedDeg = 720f;   // 초당 회전 각도 (deg/sec)

        // PlayerCharacter 와 동일 규약 (원격 추종 일관성).
        private const float k_arriveThreshold = 0.05f;    // 최종 목적지 도착 판정 거리
        private const float k_waypointThreshold = 0.25f;  // 중간 waypoint 도달 판정 거리
        private const float k_repathIntervalSec = 0.2f;   // NavMesh 경로 재계산 최소 간격
        private const float k_repathMinMoveSqr = 0.25f;   // 목적지가 0.5m 이상 바뀌어야 재계산 (sqr)
        private const float k_sampleRadius = 2.0f;        // NavMesh 위로 위치 보정 검색 반경

        private bool m_hasMoveDest = false;
        private Vector3 m_moveDest;

        // 현재 따라가는 경로 (waypoint 리스트). m_pathIndex 가 현재 향하는 waypoint.
        private readonly List<Vector3> m_path = new List<Vector3>(32);
        private int m_pathIndex;

        private float m_lastPathTime = -999f;   // 마지막 FindPath 시점 (Time.time)
        private Vector3 m_lastPathDest;          // 마지막 FindPath 목적지

        public bool IsMoving => m_hasMoveDest;

        // 공통 애니메이터. idle/move 등 의미론적 상태를 Animator 파라미터로 변환한다.
        // 없을 수 있음(아트 미적용 prefab) → 그 경우 애니메이션 없이 동작.
        private IActorAnimator m_actorAnimator;

        // MonsterFactory 가 생성 직후 1회 호출.
        public void Initialize(long objectId, long monsterKey, Vector3 pos, float dirY)
        {
            ObjectId = objectId;
            MonsterKey = monsterKey;

            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // 공통 애니메이터 해석. AddComponent 타이밍 의존을 피하려고 Awake 가 아니라
            // (모든 컴포넌트가 부착된 뒤 호출되는) Initialize 에서 찾는다. 없으면 애니메이션 없이 동작.
            m_actorAnimator = GetComponentInChildren<IActorAnimator>();
        }

        // ─── 서버 MoveNtf 처리 ───────────────────────────────────────────

        // is_moving=false: 서버 위치로 즉시 스냅 + 이동 취소.
        public void SetPosition(Vector3 pos, float dirY)
        {
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
            m_hasMoveDest = false;
            m_path.Clear();
            m_pathIndex = 0;
        }

        // is_moving=true: 목적지로 이동 시작/갱신.
        // dest 를 NavMesh 위로 보정 후 경로 계산. NavMesh 미로드 시 직선 폴백. (PlayerCharacter 와 동일)
        public void SetMoveDestination(Vector3 dest)
        {
            if (!NavMeshService.IsLoaded)
            {
                if (!m_hasMoveDest)
                    Debug.LogWarning("[MonsterObject] NavMesh 가 로드되지 않았습니다. 직선 이동으로 폴백합니다.");

                m_moveDest = dest;
                m_hasMoveDest = true;
                m_path.Clear();
                m_pathIndex = 0;
                return;
            }

            // 목적지를 NavMesh 위로 클램프 (서버 dest 가 살짝 벗어나도 안전).
            if (!NavMeshService.ClampToNavMesh(transform.position, dest, out Vector3 destOnNav))
                return;   // 클램프 실패(몬스터가 NavMesh 밖인 드문 상황). 현재 상태 유지.

            m_moveDest = destOnNav;
            m_hasMoveDest = true;

            bool needRepath =
                m_path.Count == 0 ||
                (Time.time - m_lastPathTime) >= k_repathIntervalSec ||
                (destOnNav - m_lastPathDest).sqrMagnitude >= k_repathMinMoveSqr;

            if (needRepath)
                recalculatePath(destOnNav);
        }

        public void StopMove()
        {
            m_hasMoveDest = false;
            m_path.Clear();
            m_pathIndex = 0;
        }

        private void recalculatePath(Vector3 destOnNav)
        {
            Vector3 startPos = transform.position;
            if (NavMeshService.SamplePosition(startPos, k_sampleRadius, out Vector3 startOnNav))
                startPos = startOnNav;

            bool ok = NavMeshService.FindPath(startPos, destOnNav, m_path);
            m_pathIndex = 0;
            m_lastPathTime = Time.time;
            m_lastPathDest = destOnNav;

            if (!ok)
                m_path.Clear();   // 경로 못 찾음 → 이동 중단.
        }

        // ─── 이동 시뮬레이션 ─────────────────────────────────────────────

        private void Update()
        {
            updateMovement();
            updateAnimator();
        }

        // 이동 상태(IsMoving)를 애니메이터에 반영. 값 변화 감지는 애니메이터 구현이 처리하므로 매 프레임 그대로 밀어넣는다.
        private void updateAnimator()
        {
            if (m_actorAnimator == null)
                return;

            m_actorAnimator.SetMoving(m_hasMoveDest);
        }

        private void updateMovement()
        {
            if (!m_hasMoveDest)
                return;

            // NavMesh 폴백 (직선): m_path 가 비어있고 m_moveDest 만 있는 경우.
            if (m_path.Count == 0)
            {
                moveStraightTo(m_moveDest, finalDest: true);
                return;
            }

            // 안전 가드.
            if (m_pathIndex >= m_path.Count)
            {
                StopMove();
                return;
            }

            bool isLastWaypoint = (m_pathIndex == m_path.Count - 1);
            Vector3 target = m_path[m_pathIndex];   // target.y 는 NavMesh 표면 Y.
            float threshold = isLastWaypoint ? k_arriveThreshold : k_waypointThreshold;

            Vector3 cur = transform.position;
            Vector3 diff = target - cur;
            float dist = diff.magnitude;

            if (dist < threshold)
            {
                if (isLastWaypoint)
                {
                    transform.position = target;
                    StopMove();
                    return;
                }
                m_pathIndex++;
                return;
            }

            Vector3 dir = diff / dist;
            float step = m_moveSpeed * Time.deltaTime;

            if (step >= dist)
            {
                transform.position = target;
                if (isLastWaypoint)
                    StopMove();
                else
                    m_pathIndex++;
            }
            else
            {
                transform.position = cur + dir * step;
            }

            rotateTowardsDirection(dir);
        }

        // 진행 방향으로 부드럽게 회전 (수평면, Y축 회전만).
        private void rotateTowardsDirection(Vector3 dir)
        {
            Vector3 flat = new Vector3(dir.x, 0f, dir.z);
            if (flat.sqrMagnitude < 0.0001f)
                return;

            Quaternion targetRot = Quaternion.LookRotation(flat);
            transform.rotation = Quaternion.RotateTowards(
                transform.rotation,
                targetRot,
                m_rotateSpeedDeg * Time.deltaTime);
        }

        // NavMesh 없을 때 직선 폴백 이동.
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
