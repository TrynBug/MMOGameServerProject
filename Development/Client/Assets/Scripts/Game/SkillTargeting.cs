using UnityEngine;

namespace Client.Game
{
    // 오토타게팅 모드. 스킬.md 의 "타게팅 알고리즘은 교체 가능한 구조" 요구에 맞춰 인터페이스로 둔다.
    // 1차에는 "가장 가까운 적"만 구현한다. ("최고 등급" 모드는 MonsterObject.Grade 를 읽어 나중에 추가.)
    public interface ISkillTargetingMode
    {
        // from 위치 기준 range 내에서 대상 1마리를 고른다. 없으면 null.
        MonsterObject SelectTarget(Vector3 from, float range);
    }

    // 가장 가까운 적. (기본 모드)
    public class NearestTargeting : ISkillTargetingMode
    {
        public MonsterObject SelectTarget(Vector3 from, float range)
        {
            StageManager stage = StageManager.Instance;
            if (stage == null)
                return null;

            float bestSqr = range * range;   // range 밖은 후보에서 제외.
            MonsterObject best = null;

            foreach (MonsterObject m in stage.Monsters.Values)
            {
                if (m == null || m.IsDead)
                    continue;

                float sqr = (m.transform.position - from).sqrMagnitude;
                if (sqr <= bestSqr)
                {
                    bestSqr = sqr;
                    best = m;
                }
            }
            return best;
        }
    }

    // 가장 등급이 높은 적 중 가장 가까운 적. (보스>유니크>레어>매직>노말, EMonsterGrade 값이 클수록 높음.)
    public class HighestGradeNearestTargeting : ISkillTargetingMode
    {
        public MonsterObject SelectTarget(Vector3 from, float range)
        {
            StageManager stage = StageManager.Instance;
            if (stage == null)
                return null;

            float rangeSqr = range * range;
            MonsterObject best = null;
            int bestGrade = -1;
            float bestSqr = float.MaxValue;

            foreach (MonsterObject m in stage.Monsters.Values)
            {
                if (m == null || m.IsDead)
                    continue;

                float sqr = (m.transform.position - from).sqrMagnitude;
                if (sqr > rangeSqr)
                    continue;   // range 밖 제외.

                int grade = (int)m.Grade;
                // 더 높은 등급 우선, 같은 등급이면 더 가까운 적.
                if (grade > bestGrade || (grade == bestGrade && sqr < bestSqr))
                {
                    bestGrade = grade;
                    bestSqr = sqr;
                    best = m;
                }
            }
            return best;
        }
    }
}
