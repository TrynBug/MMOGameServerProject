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
        private int m_onHitSkillKey;           // OnHit 폭발 스킬 키 (0=없음). 종료 위치에 폭발 비주얼 표시.

        // SkillSystem 이 생성 직후 1회 호출.
        public void Launch(SkillProjectileGroup group, int index, Vector3 startPos, Vector3 dir, float speed, float maxRange, int onHitSkillKey)
        {
            m_group = group;
            m_index = index;
            m_dir = dir;
            m_speed = speed;
            m_maxRange = maxRange;
            m_onHitSkillKey = onHitSkillKey;
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
            transform.position += m_dir * step;
            m_traveled += step;

            if (m_traveled >= m_maxRange)
                endAtMaxRange();
        }

        private void OnTriggerEnter(Collider other)
        {
            if (m_ended)
                return;

            MonsterObject monster = other.GetComponentInParent<MonsterObject>();
            if (monster == null)
                return;   // 몬스터가 아니면 통과 (지형/타 투사체 등).

            endOnHit(monster);
        }

        private void endOnHit(MonsterObject monster)
        {
            m_ended = true;
            Vector3 p = transform.position;
            m_group?.ReportHit(m_index, monster.ObjectId, p.x, p.z);
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }

        private void endAtMaxRange()
        {
            m_ended = true;
            Vector3 p = transform.position;
            m_group?.ReportMaxRange(m_index, p.x, p.z);
            SkillSystem.Instance?.SpawnHitExplosionVisual(m_onHitSkillKey, p, m_dir);
            Destroy(gameObject);
        }
    }
}
