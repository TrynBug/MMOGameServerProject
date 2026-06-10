using System.Collections.Generic;
using MMO.Client.Navigation;
using UnityEngine;

namespace Client.Game
{
    // 본인(LocalPlayer) 캐릭터의 클라 예측 이동을 담당한다.
    //
    // PlayerCharacter 가 소유하고, 받은 Transform 을 직접 움직인다. 매 프레임 Tick(moveSpeed) 호출.
    //   책임: 목적지/경로(navmesh) 추적, 스킬 강제이동(대시/블링크), 진행방향 회전, 이동 잠금.
    //   책임 아님: 서버 화해(PlayerReconciler), 애니메이션/원격 보간(PlayerCharacter).
    //
    // 이동 시뮬레이션은 서버 Character::Update 와 동일한 규칙(waypoint while-루프 소모, 스킬 ease-out
    // f=1-(1-t)^2)을 따라야 위치가 동기화된다.
    public class LocalPlayerMover
    {
        private readonly Transform m_tf;
        private readonly float m_rotateSpeedDeg;   // 초당 회전 각도 (deg/sec).

        private const float k_arriveThreshold = 0.05f;               // 최종 목적지 도착 판정 거리
        private const float k_waypointThreshold = 0.25f;             // 중간 waypoint 도달 판정 거리
        // repath throttle 을 사실상 제거(0)한다. 홀드 이동 중 커서 지면점(dest)은 매 프레임 조금씩
        // 움직이는데, throttle 이 있으면 dest 가 0.5m 움직일 때마다 끊겨서 갱신되어 heading 이 계단식으로
        // 떨린다. 매 프레임 repath 하면 dest 를 연속 추적해 heading 이 매끄럽다. 본인 캐릭터 1개만
        // 길찾기하므로 매 프레임 FindPath 비용은 무시할 수준. (FindPath 실패 시 직선 폴백이 처리.)
        private const float k_repathIntervalSec = 0f;                // 0 = 매 프레임 재계산(연속 dest 추적)
        private const float k_repathMinMoveSqr = 0f;                 // 0 = dest 변화량 무관(간격 조건만으로 매 프레임)
        private const float k_sampleRadius = 2.0f;                   // NavMesh 위로 위치 보정할 때 검색 반경

        private bool m_hasMoveDest = false;
        private Vector3 m_moveDest;

        // 현재 따라가는 경로 (waypoint 리스트). pathIndex 가 현재 향하고 있는 waypoint 의 인덱스.
        private readonly List<Vector3> m_path = new List<Vector3>(32);
        private int m_pathIndex;

        private float m_lastPathTime = -999f;   // 마지막으로 FindPath 를 호출한 시점 (Time.time)
        private Vector3 m_lastPathDest;         // 마지막으로 FindPath 에 사용한 목적지

        // ── 스킬 강제이동 (대시/블링크) ──
        // 일반 이동(m_hasMoveDest)과 별개. m_skillMoving 동안 Tick 이 ease-out 으로 전진.
        // 서버 Character::ApplySkillMovement 와 동일한 공식(f=1-(1-t)^2)을 사용한다.
        private bool m_skillMoving;
        private Vector3 m_skillMoveStart;
        private Vector3 m_skillMoveDir;        // 정규화 평면 방향
        private float m_skillMoveDistance;
        private float m_skillMoveDuration;
        private float m_skillMoveElapsed;

        // 이동 잠금 만료 시각(Time.time). Stationary 시전 동안 이동 입력을 막는다.
        private float m_moveLockedUntil;

        public bool IsMoving => m_hasMoveDest;
        public bool IsSkillMoving => m_skillMoving;

        public LocalPlayerMover(Transform tf, float rotateSpeedDeg)
        {
            m_tf = tf;
            m_rotateSpeedDeg = rotateSpeedDeg;
        }

        // 목적지 설정. 마우스 누르고 있는 동안 매 프레임 갱신됨.
        // - 입력된 dest 는 NavMesh 위로 보정한다 (마우스가 절벽 아래 등을 가리킬 수도 있어서).
        // - NavMesh 가 로드되지 않은 경우 직선 이동으로 폴백.
        // - 경로 재계산은 매 프레임 (k_repath* = 0).
        public void SetDestination(Vector3 dest)
        {
            if (m_skillMoving)
                return;   // 스킬 강제이동(대시/블링크) 중에는 일반 이동 입력 무시.
            if (Time.time < m_moveLockedUntil)
                return;   // 시전 중 이동 잠금 (Stationary 캐스트).
            // dest 의 y 는 그대로 둔다. NavMesh 검색 시 자동으로 표면 Y 로 보정됨.

            if (!NavMeshService.IsLoaded)
            {
                // NavMesh 없음 -> 직선 이동 폴백.
                // 한 번만 경고 (스팸 방지를 위해 m_hasMoveDest 가 false 일 때만).
                if (!m_hasMoveDest)
                    Debug.LogWarning("[LocalPlayerMover] NavMesh 가 로드되지 않았습니다. 직선 이동으로 폴백합니다.");

                m_moveDest = dest;
                m_hasMoveDest = true;
                m_path.Clear();
                m_pathIndex = 0;
                return;
            }

            // 마우스가 NavMesh 바깥을 가리키면 "캐릭터가 갈 수 있는 가장 먼 점"으로 클램프.
            // 이렇게 하면 마우스를 어디에 놓든 캐릭터가 그 방향으로 갈 수 있는 만큼 이동한다 (클릭 무브 게임의 표준 방식).
            // destOnNav 에는 NavMesh 표면의 정확한 Y 값이 들어있으므로 그대로 사용.
            if (!NavMeshService.ClampToNavMesh(m_tf.position, dest, out Vector3 destOnNav))
            {
                // 클램프도 실패 (캐릭터가 NavMesh 위에 없는 드문 상황).
                // 현재 이동 상태 유지하고 이번 입력은 무시.
                return;
            }

            m_moveDest = destOnNav;
            m_hasMoveDest = true;

            // 경로 재계산 여부 판정.
            bool needRepath =
                m_path.Count == 0 ||
                (Time.time - m_lastPathTime) >= k_repathIntervalSec ||
                (destOnNav - m_lastPathDest).sqrMagnitude >= k_repathMinMoveSqr;

            if (needRepath)
                recalculatePath(destOnNav);
        }

        // 즉시 멈춤. (마우스 버튼을 뗐을 때)
        public void Stop()
        {
            m_hasMoveDest = false;
            m_path.Clear();
            m_pathIndex = 0;
        }

        // 위치 강제 동기화(텔레포트/화해 하드스냅) 시 이동 상태 전체 취소.
        public void CancelForTeleport()
        {
            m_hasMoveDest = false;   // 위치 강제 동기화는 이동 취소
            m_skillMoving = false;   // 스킬 강제이동도 취소
            m_path.Clear();
            m_pathIndex = 0;
        }

        // 이동을 seconds 동안 잠그고(Stationary 시전), 현재 이동도 즉시 멈춘다.
        public void LockMovement(float seconds)
        {
            m_moveLockedUntil = Time.time + seconds;
            Stop();
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
            Stop();
            m_tf.rotation = Quaternion.LookRotation(d);

            if (durationSec <= 0f)
            {
                // 즉시 블링크: 끝점 스냅 (Y 유지).
                Vector3 end = m_tf.position + d * distance;
                end.y = m_tf.position.y;
                m_tf.position = end;
                m_skillMoving = false;
                return;
            }

            m_skillMoveStart = m_tf.position;
            m_skillMoveDir = d;
            m_skillMoveDistance = distance;
            m_skillMoveDuration = durationSec;
            m_skillMoveElapsed = 0f;
            m_skillMoving = true;
        }

        // 1프레임 이동 전진. moveSpeed 는 호출자(PlayerCharacter.GetMoveSpeed)가 넘긴다.
        public void Tick(float moveSpeed)
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
                moveStraightTo(m_moveDest, finalDest: true, moveSpeed: moveSpeed);
                return;
            }

            // NavMesh waypoint 따라가기. 서버 시뮬레이션과 동일하게, 이번 프레임 이동량(remain)을
            // while 루프로 소모하여 한 프레임에 여러 waypoint 를 이어서 통과한다.
            float remain = moveSpeed * Time.deltaTime;
            Vector3 lastDir = Vector3.zero;

            while (remain > 0f)
            {
                // 안전 가드: path 가 비었거나 인덱스가 끝을 넘으면 정지.
                if (m_pathIndex >= m_path.Count)
                {
                    m_hasMoveDest = false;
                    m_path.Clear();
                    m_pathIndex = 0;
                    break;
                }

                bool isLastWaypoint = (m_pathIndex == m_path.Count - 1);
                Vector3 target = m_path[m_pathIndex];   // target.y 는 NavMesh 표면 Y.
                Vector3 cur = m_tf.position;
                Vector3 diff = target - cur;
                float dist = diff.magnitude;

                if (dist <= 1e-4f)
                {
                    // 이미 이 waypoint 위. 다음으로 진행(마지막이면 정지).
                    if (isLastWaypoint)
                    {
                        m_hasMoveDest = false;
                        m_path.Clear();
                        m_pathIndex = 0;
                        break;
                    }
                    m_pathIndex++;
                    continue;
                }

                Vector3 dir = diff / dist;
                lastDir = dir;

                if (dist <= remain)
                {
                    // 이번 프레임에 이 waypoint 도달. 정확히 스냅하고 자투리(remain)로 다음 구간 계속.
                    m_tf.position = target;
                    remain -= dist;
                    if (isLastWaypoint)
                    {
                        m_hasMoveDest = false;
                        m_path.Clear();
                        m_pathIndex = 0;
                        break;
                    }
                    m_pathIndex++;
                    continue;
                }

                // 이번 프레임엔 도달 못 함. 방향으로 remain 만큼 전진하고 종료.
                m_tf.position = cur + dir * remain;
                remain = 0f;
            }

            // 진행 방향을 바라보기 (수평면에서만 회전, y축 회전만). 부드럽게 보간.
            if (lastDir.sqrMagnitude > 0f)
                rotateTowardsDirection(lastDir);
        }

        // 강제이동 1 프레임 전진. ease-out(f=1-(1-t)^2), 종료 시 끝점 스냅.
        private void advanceSkillMove()
        {
            m_skillMoveElapsed += Time.deltaTime;
            float t = (m_skillMoveDuration > 0f) ? m_skillMoveElapsed / m_skillMoveDuration : 1f;

            if (t >= 1f)
            {
                Vector3 end = m_skillMoveStart + m_skillMoveDir * m_skillMoveDistance;
                end.y = m_skillMoveStart.y;
                m_tf.position = end;
                m_skillMoving = false;
                return;
            }

            float f = 1f - (1f - t) * (1f - t);
            Vector3 p = m_skillMoveStart + m_skillMoveDir * (m_skillMoveDistance * f);
            p.y = m_skillMoveStart.y;
            m_tf.position = p;
        }

        // NavMesh 경로 재계산. 시작점은 현재 캐릭터 위치 (NavMesh 위로 보정해서).
        private void recalculatePath(Vector3 destOnNav)
        {
            // 시작점도 NavMesh 위로 보정 (캐릭터가 살짝 떠 있거나 가라앉아도 안전).
            Vector3 startPos = m_tf.position;
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
                return;
            }

            // FindPath 의 첫 점은 보통 시작 위치 자체(navmesh 재투영점)다. 현재 위치와 거의 같은 선두
            // waypoint 들을 스킵한다. 안 그러면 잦은 repath(홀드 이동 중)마다 캐릭터가 재투영된 시작점으로
            // 미세하게 끌려가 heading 이 좌우로 떨린다(이동량 크기는 정상이라 눈에 안 띄지만 시각적 떨림).
            // 서버 setDestination 의 선두 waypoint 스킵과 동일한 처리.
            float px = m_tf.position.x;
            float pz = m_tf.position.z;
            while (m_pathIndex < m_path.Count)
            {
                float dx = m_path[m_pathIndex].x - px;
                float dz = m_path[m_pathIndex].z - pz;
                if (dx * dx + dz * dz > k_waypointThreshold * k_waypointThreshold)
                    break;   // 의미 있는 거리의 waypoint — 여기서부터 따라간다.
                m_pathIndex++;
            }
            if (m_pathIndex >= m_path.Count)
            {
                // 모든 점이 현재 위치 근처 → 따라갈 waypoint 없음. 직선 폴백(moveStraightTo)이 처리하도록 비운다.
                m_path.Clear();
                m_pathIndex = 0;
            }
        }

        // 주어진 방향(dir, 수평면)으로 캐릭터를 부드럽게 회전시킨다.
        // RotateTowards 는 "초당 N도" 일정한 각속도로 회전하므로 직관적이고 예측 가능.
        private void rotateTowardsDirection(Vector3 dir)
        {
            // 수평면 성분만 계산 (Y 제외).
            Vector3 flat = new Vector3(dir.x, 0f, dir.z);
            if (flat.sqrMagnitude < 0.0001f)
                return;

            Quaternion targetRot = Quaternion.LookRotation(flat);
            m_tf.rotation = Quaternion.RotateTowards(
                m_tf.rotation,
                targetRot,
                m_rotateSpeedDeg * Time.deltaTime);
        }

        // NavMesh 없을 때의 폴백 이동 (이전 동작과 동일).
        private void moveStraightTo(Vector3 target, bool finalDest, float moveSpeed)
        {
            Vector3 cur = m_tf.position;
            Vector3 diff = target - cur;
            float dist = diff.magnitude;

            if (dist < k_arriveThreshold)
            {
                m_tf.position = target;
                if (finalDest) m_hasMoveDest = false;
                return;
            }

            Vector3 dir = diff / dist;
            float step = moveSpeed * Time.deltaTime;

            if (step >= dist)
            {
                m_tf.position = target;
                if (finalDest) m_hasMoveDest = false;
            }
            else
            {
                m_tf.position = cur + dir * step;
            }

            rotateTowardsDirection(dir);
        }
    }
}
