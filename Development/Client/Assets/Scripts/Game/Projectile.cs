using UnityEngine;

namespace Client.Game
{
    // 1개 투사체의 비행 + hit 판정.
    // SkillSystem 이 placeholder 비주얼(프리미티브 구)에 부착해 생성한다.
    //
    // 종료 조건:
    //   - 몬스터 트리거 진입(직격): 그룹에 직격 보고 후 제거. (1투사체 1타겟)
    //   - 최대사거리 도달: 그룹에 최대사거리 보고 후 제거.
    // m_group 이 null 이면 비주얼 전용(다른 캐스터 재현) — 보고 없이 제거만 한다.
    //
    // 트리거 발생 조건: 이 투사체가 isTrigger 콜라이더 + kinematic Rigidbody 를 갖는다(SkillSystem 이 셋업).
    public class Projectile : MonoBehaviour
    {
        private SkillProjectileGroup m_group;   // 보고 대상 (비주얼 전용이면 null)
        private int m_index;                    // 그룹 내 투사체 index (서버 fan dir 순서와 정합)
        private Vector3 m_dir;                  // 정규화 X-Z 진행 방향
        private float m_speed;
        private float m_maxRange;
        private float m_traveled;
        private bool m_ended;
        private int m_sourceSkillKey;          // 투사체 본체 스킬 키. 종료(hit) 위치에서 적중음(SfxHit) 재생에 사용.
        private int m_onHitSkillKey;           // OnHit 폭발 스킬 키 (0=없음). 종료 위치에 폭발 비주얼 표시.
        private bool m_ignoreMonsters;         // true = 몬스터 충돌 무시(몬스터 시전 투사체. 비주얼 전용 + 시전자 자가충돌 방지).

        // 몬스터 시전 투사체가 "플레이어에 닿았다"고 보는 X-Z 반경. 플레이어엔 콜라이더가 없어 거리로 판정(비주얼 종료용).
        private const float k_monsterHitRadius = 0.7f;

        // SkillSystem 이 생성 직후 1회 호출.
        // ignoreMonsters: 몬스터가 쏜 투사체(타겟=플레이어)면 true. OnTriggerEnter 가 몬스터(시전자 포함)에 반응하지 않게 한다.
        public void Launch(SkillProjectileGroup group, int index, Vector3 startPos, Vector3 dir, float speed, float maxRange, int sourceSkillKey, int onHitSkillKey, bool ignoreMonsters = false)
        {
            m_group = group;
            m_index = index;
            m_dir = dir;
            m_speed = speed;
            m_maxRange = maxRange;
            m_sourceSkillKey = sourceSkillKey;
            m_onHitSkillKey = onHitSkillKey;
            m_ignoreMonsters = ignoreMonsters;
            m_traveled = 0f;
            m_ended = false;
            transform.position = startPos;
            if (dir.sqrMagnitude > 0.0001f)
                transform.rotation = Quaternion.LookRotation(dir);
        }

        private void Update()
        {
            if (m_ended)
                return;

            float step = m_speed * Time.deltaTime;
            Vector3 prev = transform.position;

            // 지형·정적 장애물 차단: 이전→현 세그먼트가 벽(지형 둔덕/바위 등)을 물면 그 자리에서 소멸(벽 통과 방지).
            // QueryTriggerInteraction.Ignore 로 자기/타 투사체·몬스터 트리거는 무시하고 solid 지오메트리만 검사한다.
            // 충돌 위치를 서버에 exploded_on_terrain 으로 보고 → 서버가 그 자리에 폭발을 발동(적중은 서버 판정).
            if (Physics.Raycast(prev, m_dir, out RaycastHit hit, step, GameLayers.ProjectileBlockerMask, QueryTriggerInteraction.Ignore))
            {
                endBlockedByTerrain(hit.point);
                return;
            }

            transform.position = prev + m_dir * step;
            m_traveled += step;

            // 몬스터 시전 투사체: 근처 플레이어에 닿으면 그 자리에서 비주얼 종료(서버가 대미지 권위, 몬스터는 무시).
            if (m_ignoreMonsters && StageManager.Instance != null
                && StageManager.Instance.FindCharacterInRadiusXZ(transform.position, k_monsterHitRadius) != null)
            {
                endVisualAt(transform.position);
                return;
            }

            if (m_traveled >= m_maxRange)
                endAtMaxRange();
        }

        // 비주얼 종료(그룹 보고 없음). 몬스터 투사체가 플레이어에 닿았을 때.
        private void endVisualAt(Vector3 p)
        {
            m_ended = true;
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_sourceSkillKey, m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }

        // 지형/정적 장애물에 막혀 종료. 충돌 위치를 그룹에 보고(exploded_on_terrain)하고 적중 비주얼/사운드를 낸다.
        // 서버는 그 위치에 OnHit 폭발을 발동하고 폭발 적중을 판정한다(폭발 중심=벽 위치라 벽 너머 딜 없음).
        private void endBlockedByTerrain(Vector3 p)
        {
            m_ended = true;
            m_group?.ReportBlocked(m_index, p.x, p.z);
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_sourceSkillKey, m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }

        private void OnTriggerEnter(Collider other)
        {
            if (m_ended)
                return;

            // 몬스터 시전 투사체(타겟=플레이어)는 몬스터 충돌을 무시한다. (서버 권위 판정, 비주얼 전용.)
            // 안 그러면 시전자 몬스터 위에서 생성되자마자 자가충돌로 즉시 사라진다.
            if (m_ignoreMonsters)
                return;

            MonsterObject monster = other.GetComponentInParent<MonsterObject>();
            if (monster == null)
                return;   // 몬스터가 아니면 통과 (지형/타 투사체 등).

            // 죽은(시체) 몬스터엔 부딪히지 않고 통과한다. 시체 콜라이더는 남아있다(디스폰 전까지)
            if (monster.IsDead)
                return;

            endOnHit(monster);
        }

        private void endOnHit(MonsterObject monster)
        {
            m_ended = true;
            Vector3 p = transform.position;
            m_group?.ReportHit(m_index, monster.ObjectId, p.x, p.z);
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_sourceSkillKey, m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }

        private void endAtMaxRange()
        {
            m_ended = true;
            Vector3 p = transform.position;
            m_group?.ReportMaxRange(m_index, p.x, p.z);
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_sourceSkillKey, m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }
    }
}
