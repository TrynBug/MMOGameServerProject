using System.Collections.Generic;
using GamePacket;

namespace Client.Game
{
    // 1회 시전으로 발사된 투사체 묶음 (시전 클라 전용).
    //
    // 서버 SkillCastNtf 의 effect_id 를 받기 전까지 hit 보고를 버퍼링한다.
    // (레이턴시로 투사체가 effect_id 도착보다 먼저 끝날 수 있으므로.)
    // SkillSystem 이 effect_id 를 바인딩하고, 버퍼를 모아 SkillProjectileHitReq 로 플러시한다.
    //
    // 참고: 5c(서버 게이팅) 도입 전이라 시전이 거부되는 경로가 없어 effect_id 는 항상 도착한다.
    //       (거부 경로가 생기면 미바인딩 그룹의 타임아웃 정리가 필요해질 수 있음.)
    public class SkillProjectileGroup
    {
        public int SkillKey { get; }
        public long EffectId { get; private set; }   // 0 = 아직 서버 effect_id 미수신
        public bool HasEffectId => EffectId != 0;

        // 모든 투사체가 발사되고(MarkLaunched) 종료됐으며 보낼 hit 도 없으면 그룹 제거 가능.
        // m_launched 가 false 인 동안(CastDelay 대기 중)에는 절대 완료로 간주하지 않는다.
        public bool IsDone => m_launched && m_aliveCount <= 0 && m_pending.Count == 0;

        private bool m_launched;    // spawnFan 이 실제 발사를 끝냈는지
        private int m_aliveCount;   // 아직 종료되지 않은 투사체 수
        private readonly List<SkillHitItem> m_pending = new List<SkillHitItem>();

        public SkillProjectileGroup(int skillKey)
        {
            SkillKey = skillKey;
        }

        // 발사 완료 시점에 실제 발사된 투사체 수를 확정한다. (prefab 로드 실패 시 count=0)
        public void MarkLaunched(int count)
        {
            m_aliveCount = count;
            m_launched = true;
        }

        public void AssignEffectId(long effectId)
        {
            EffectId = effectId;
        }

        // 직격 종료 보고.
        public void ReportHit(int index, long targetId, float hitX, float hitZ)
        {
            --m_aliveCount;
            m_pending.Add(new SkillHitItem
            {
                ProjectileIndex = index,
                TargetObjectId = targetId,
                HitX = hitX,
                HitZ = hitZ,
            });
        }

        // 지형/정적 장애물에 막혀 종료. 서버로 보낼 hit 이 없다(벽 너머 대미지 방지) — 생존 카운트만 감소.
        public void ReportBlocked(int index)
        {
            --m_aliveCount;
        }

        // 최대사거리 종료 보고 (직격 대상 없음. 폭발 적중은 서버가 판정).
        public void ReportMaxRange(int index, float hitX, float hitZ)
        {
            --m_aliveCount;
            m_pending.Add(new SkillHitItem
            {
                ProjectileIndex = index,
                TargetObjectId = 0,
                ExplodedAtMaxRange = true,
                HitX = hitX,
                HitZ = hitZ,
            });
        }

        // effect_id 바인딩 후 호출. 버퍼된 hit 들에 EffectId 를 채워 꺼낸다(없으면 null).
        public List<SkillHitItem> DrainPending()
        {
            if (m_pending.Count == 0)
                return null;

            foreach (SkillHitItem item in m_pending)
                item.EffectId = EffectId;

            List<SkillHitItem> drained = new List<SkillHitItem>(m_pending);
            m_pending.Clear();
            return drained;
        }
    }
}
