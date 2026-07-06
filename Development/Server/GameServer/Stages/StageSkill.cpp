#include "pch.h"
#include "Stages/Stage.h"
#include "StageObjects/ActorObject.h"
#include "StageObjects/Monster.h"
#include "Stages/StageScript.h"
#include "GameServer.h"
#include "Enum/GameEnum_Common.h"
#include "Generated/GameData_Monster.h"
#include "Skills/EffectShape.h"
#include "Skills/EffectParams.h"
#include "Skills/AreaEffect.h"
#include "Skills/ProjectileGroup.h"
#include "Skills/MonsterProjectile.h"
#include "Skills/SkillBake.h"
#include "Generated/GameData_Skill.h"

#include <cmath>
#include <random>

// ─────────────────────────────────────────────────────────────
// Stage 의 스킬 효과 서브시스템.
// 범위공격(AreaEffect) / 투사체 그룹(ProjectileGroup) 의 spawn·tick·만료·대미지·시전통보를 담당한다.
// ─────────────────────────────────────────────────────────────

namespace
{
    // 투사체 hit 보고 사거리 sanity 검증 여유 (units, X-Z). 정밀 핵검사는 후속.
    constexpr float k_projectileRangeToleranceXZ = 3.0f;
}

// (centerPos 를 중심으로) shape 범위 안의 "적" StageObject 들을 outEnemies 에 채운다. (X-Z 평면)
// 진영 규칙(v1): Monster 시전자는 User(캐릭터)를, 그 외(User 등) 시전자는 Monster 를 대상으로 한다.
void Stage::QueryEnemiesInShape(EObjectType casterType, const Vector3& centerPos, const EffectShape& shape,
                                std::vector<StageObject*>& outEnemies, bool requireLineOfSight)
{
    outEnemies.clear();

    if (shape.type == ESkillEffectShape::None)
        return;

    // 적 타입 결정 (v1 진영 규칙).
    const EObjectType enemyType = (casterType == EObjectType::Monster) ? EObjectType::User : EObjectType::Monster;

    // center 가 속한 섹터 + 모양의 bounding 반경으로 검사할 섹터 range 를 정한다.
    int32 centerSectorX = 0;
    int32 centerSectorZ = 0;
    if (!GetSectorIndex(centerPos.x, centerPos.z, centerSectorX, centerSectorZ))
        return;   // center 가 맵 영역 밖이면 대상 없음

    const float boundingRadius = shape.GetBoundingRadiusXZ();
    const int32 sectorRange = static_cast<int32>(std::ceil(boundingRadius / static_cast<float>(GetSectorSize())));

    ForEachAdjacentSector(centerSectorX, centerSectorZ, sectorRange,
        [&](Sector* pSector)
        {
            const std::unordered_map<int64, StageObject*>& candidates =
                (enemyType == EObjectType::Monster) ? pSector->GetMonsters() : pSector->GetUsers();

            for (const auto& [objectId, pObject] : candidates)
            {
                // 사망한 대상은 타게팅에서 제외 (시체로 남아 있어도 스킬 대상이 되지 않음).
                if (static_cast<ActorObject*>(pObject)->IsDead())
                    continue;
                const Vector3 objPos(pObject->GetPosX(), pObject->GetPosY(), pObject->GetPosZ());
                if (!shape.Contains(centerPos, objPos))
                    continue;
                // 벽 너머(시야 차단) 대상 제외 — 폭발/범위가 벽을 넘지 못하게 한다.
                // shape 판정을 통과한 대상에만 raycast 하므로 비용은 실제 범위 내 후보 수에 비례.
                if (requireLineOfSight
                    && !HasLineOfSight(centerPos.x, centerPos.y, centerPos.z, objPos.x, objPos.y, objPos.z))
                    continue;
                outEnemies.push_back(pObject);
            }
        });
}

// SkillComponent(또는 폭발 경로)가 bake 한 EffectParams 로 AreaEffect 를 생성해 월드에 등록한다.
// scatterCount > 1 이면 origin 기준 링 영역에 시드 랜덤으로 N개를 분산 배치한다 (메테오 파편).
void Stage::SpawnSkillAreaEffect(const EffectParams& params)
{
    if (params.scatterCount <= 1)
    {
        m_skillAreaEffects.push_back(std::make_unique<AreaEffect>(params));
        return;
    }

    // [inner, outer] 링 영역에 면적 균등 분포로 scatterCount 개 배치. seed 로 결정론적 재현.
    std::mt19937 rng(params.seed);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> rSqDist(
        params.scatterInnerRadius * params.scatterInnerRadius,
        params.scatterOuterRadius * params.scatterOuterRadius);

    for (int32 i = 0; i < params.scatterCount; ++i)
    {
        const float angle = angleDist(rng);
        const float r = std::sqrt(rSqDist(rng));

        EffectParams sub = params;
        sub.scatterCount = 0;   // 분산된 각 조각은 다시 분산하지 않는다
        sub.origin = Vector3(params.origin.x + std::cos(angle) * r,
                             params.origin.y,
                             params.origin.z + std::sin(angle) * r);
        m_skillAreaEffects.push_back(std::make_unique<AreaEffect>(sub));
    }
}

// 진행 중인 AreaEffect 들을 tick 하고, 만료된 것을 swap-and-pop 으로 제거한다. 만료된 투사체 그룹도 제거한다.
void Stage::updateSkillEffects(int64 deltaMs)
{
    for (size_t i = 0; i < m_skillAreaEffects.size(); )
    {
        const bool expired = m_skillAreaEffects[i]->Update(this, deltaMs);
        if (expired)
        {
            m_skillAreaEffects[i] = std::move(m_skillAreaEffects.back());
            m_skillAreaEffects.pop_back();
        }
        else
        {
            ++i;
        }
    }

    // 만료된 투사체 그룹 제거 (Stage 시계 기준). 투사체는 tick 하지 않고 만료만 sweep 한다.
    for (auto iter = m_skillProjectileGroups.begin(); iter != m_skillProjectileGroups.end(); )
    {
        if (iter->second->IsExpired(m_stageClockMs))
            iter = m_skillProjectileGroups.erase(iter);
        else
            ++iter;
    }

    // 서버 권위 몬스터 투사체: 매 tick 전진+충돌 판정. 만료(적중/최대사거리)된 것은 swap-and-pop 제거.
    for (size_t i = 0; i < m_monsterProjectiles.size(); )
    {
        if (m_monsterProjectiles[i]->Update(this, deltaMs))
        {
            m_monsterProjectiles[i] = std::move(m_monsterProjectiles.back());
            m_monsterProjectiles.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

// 서버 권위 몬스터 투사체를 월드에 등록한다 (매 tick updateSkillEffects 에서 전진+충돌).
void Stage::SpawnMonsterProjectile(const EffectParams& params)
{
    m_monsterProjectiles.push_back(std::make_unique<MonsterProjectile>(params));
}

// 몬스터 투사체 1 tick: Linear 전진 → hit 반경 안 적 검사 → 적중/최대사거리 시 (폭발 후) 소멸.
// 정의를 여기 두는 이유: Stage 완전타입 + 효과 파이프라인(QueryEnemiesInShape/ApplyEffectDamage/bake)이 필요.
bool MonsterProjectile::Update(Stage* pStage, int64 deltaMs)
{
    if (m_expired)
        return true;

    m_elapsedMs += deltaMs;
    const Vector3 pos = CalcEffectPosition(m_params, m_params.dir, m_elapsedMs);

    // 충돌: hit 반경(shape.radius, 0 이면 작은 기본값) 안의 적(진영 규칙: Monster→User) 검사.
    EffectShape hit;
    hit.type   = ESkillEffectShape::Circle;
    hit.radius = (m_params.shape.radius > 0.0f) ? m_params.shape.radius : 0.5f;

    std::vector<StageObject*> enemies;
    pStage->QueryEnemiesInShape(m_params.casterObjectType, pos, hit, enemies);

    const bool hitSomething = !enemies.empty();
    if (hitSomething)
    {
        // 1투사체 1적: 가장 먼저 잡힌 적에게 직격. (QueryEnemiesInShape 가 사망 대상은 이미 제외.)
        pStage->ApplyEffectDamage(*static_cast<ActorObject*>(enemies[0]),
                                  m_params.damageAmount, m_params.casterObjectId, /*isDuplicate*/ false, m_params.skillKey);
    }

    const bool reachedMax = (pos - m_params.origin).LengthSqXZ() >= m_params.maxRange * m_params.maxRange;

    if (hitSomething || reachedMax)
    {
        // 폭발 연계 (OnHitSkillKey): 적중/최대사거리 위치에 폭발 스킬을 발동한다 (적중은 서버 판정).
        if (m_params.onHitSkillKey != 0)
        {
            if (const GameData_Skill* pExplosion = GameDataTable_Skill::FindData(m_params.onHitSkillKey))
            {
                EffectParams explosionParams = BakeSkillEffectParams(
                    *pExplosion, m_params.casterObjectType, m_params.casterObjectId, pos, m_params.dir, m_params.seed);
                pStage->SpawnSkillAreaEffect(explosionParams);
            }
        }
        m_expired = true;
        return true;
    }

    return false;
}

// 효과(스킬)에 의한 대미지를 target 의 현재 HP 에서 감소시키고, 주변 AOI 유저들에게 SkillDamageNtf 를 broadcast 한다.
void Stage::ApplyEffectDamage(ActorObject& target, double damage, int64 killerObjectId, bool isDuplicate, int32 sourceSkillKey)
{
    // 이미 사망한 대상에는 추가 대미지를 적용하지 않는다 (사망 후 같은 틱의 잔여 투사체 등 차단).
    if (target.IsDead())
        return;

    target.SetCurHp(target.GetCurHp() - damage);

    const double remainingHp = target.GetCurHp();

    // HP 가 0 이하로 떨어졌으면 이번 호출에서 사망 전환 (1회만 true).
    const bool justDied = (remainingHp <= 0.0) && target.MarkDead(killerObjectId);

    // 몬스터가 피격당하고 생존했으면 공격자에게 반격(어그로). 어그로 범위 밖에서 맞아도 추격하게 한다.
    // (무교전일 때만 타겟팅하는 정책은 Monster::OnDamagedBy 가 판단.)
    if (!justDied && target.GetObjectType() == EObjectType::Monster)
        static_cast<Monster&>(target).OnDamagedBy(killerObjectId);

    GameServer& server = GameServer::Instance();

    // killerObjectId = 공격자. 클라가 방향 피격표식/연출 분기에 attacker/sourceSkillKey 사용.
    m_aoiUserScratch.clear();
    ForEachUserInAoi(target.GetCurSectorX(), target.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    server.GetPacketSender().SendSkillDamageNtf(m_aoiUserScratch, target.GetObjectId(), damage, isDuplicate, remainingHp,
                                                killerObjectId, sourceSkillKey);

    // 새로 사망했으면 별도 사망 통보 (대미지 외 사인도 있을 수 있어 SkillDamageNtf 와 분리).
    // 시체는 일정 시간(corpse) 유지 후 Stage::updateMonsters 가 디스폰한다.
    if (justDied)
    {
        BroadcastObjectDeathNtf(target, killerObjectId);

        // 몬스터 사망이면 스크립트 콜백 (watch 필터는 StageScript 내부). 대량몹 부하 방지를 위해 watch 등록분만 Lua 진입.
        if (m_pScript && target.GetObjectType() == EObjectType::Monster)
        {
            const Monster& monster = static_cast<const Monster&>(target);
            m_pScript->CallOnMonsterDead(target.GetObjectId(), monster.GetMonsterData()->Key,
                                         monster.GetSpawnerKey(), killerObjectId);
        }
    }
}

// 사망한 대상 주변 AOI 유저들에게 사망을 통보(ObjectDeathNtf). 클라 사망 연출용.
void Stage::BroadcastObjectDeathNtf(const ActorObject& actor, int64 killerObjectId)
{
    m_aoiUserScratch.clear();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    GameServer::Instance().GetPacketSender().SendObjectDeathNtf(m_aoiUserScratch, actor.GetObjectId(), killerObjectId);
}

// 부활한 대상 주변 AOI 유저들에게 부활을 통보(ObjectReviveNtf). 위치/HP/MP 를 실어 보낸다.
void Stage::BroadcastObjectReviveNtf(const ActorObject& actor)
{
    m_aoiUserScratch.clear();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    GameServer::Instance().GetPacketSender().SendObjectReviveNtf(
        m_aoiUserScratch, actor.GetObjectId(),
        actor.GetPosX(), actor.GetPosY(), actor.GetPosZ(), actor.GetYaw(),
        actor.GetCurHp(), actor.GetCurMp());
}

// 코스메틱 액션(점프/감정표현)을 주변 AOI 유저들에게 relay(ActorActionNtf). 연출 전용, 게임 로직 무관.
void Stage::BroadcastActorActionNtf(const ActorObject& actor, int32 actionId, const std::string& param)
{
    m_aoiUserScratch.clear();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    GameServer::Instance().GetPacketSender().SendActorActionNtf(m_aoiUserScratch, actor.GetObjectId(), actionId, param);
}

// SkillComponent 가 bake 한 EffectParams + 부채꼴 방향으로 투사체 그룹을 생성/등록하고, 발급된 effectId 를 리턴한다.
int64 Stage::SpawnSkillProjectileGroup(const EffectParams& params, const std::vector<Vector3>& dirs)
{
    EffectParams p = params;

    // effectId 발급 (클라가 hit 보고 시 이 id 로 그룹을 지목한다).
    p.effectId = GameServer::Instance().GenerateObjectId();

    const int64 effectId = p.effectId;
    auto spGroup = std::make_unique<ProjectileGroup>();
    spGroup->Init(p, dirs, m_stageClockMs);
    m_skillProjectileGroups[effectId] = std::move(spGroup);
    return effectId;
}

ProjectileGroup* Stage::findSkillProjectileGroup(int64 effectId)
{
    auto iter = m_skillProjectileGroups.find(effectId);
    if (iter == m_skillProjectileGroups.end())
        return nullptr;
    return iter->second.get();
}

// 클라가 보고한 투사체 종료 사건을 처리한다 (직격 대미지 + 폭발). 폭발 적중은 서버(AreaEffect)가 판정한다.
void Stage::OnSkillProjectileHit(int64 effectId, int32 projectileIndex, int64 targetId,
                                 bool explodedAtMaxRange, bool explodedOnTerrain,
                                 float hitX, float hitZ)
{
    ProjectileGroup* pGroup = findSkillProjectileGroup(effectId);
    if (pGroup == nullptr || pGroup->IsExpired(m_stageClockMs))
        return;   // 이미 만료/폐기된 투사체 → 무시

    const EffectParams& params = pGroup->GetParams();

    Vector3 explosionPos;
    double  multiplier = 1.0;

    if (explodedOnTerrain)
    {
        if (!pGroup->TryConsume(projectileIndex))
            return;
        explosionPos = Vector3(hitX, 0.0f, hitZ);   // 서버가 위치를 모르므로 클라 보고 위치 사용
    }
    else if (explodedAtMaxRange)
    {
        if (!pGroup->TryConsume(projectileIndex))
            return;
        explosionPos = params.origin + pGroup->GetDir(projectileIndex) * params.maxRange;
    }
    else
    {
        // 직격.
        if (targetId == 0)
            return;   // 대상을 못 찾은 오류 보고 무시

        const ProjectileHitResult hit = pGroup->TryHit(projectileIndex, targetId);
        if (!hit.accepted)
            return;   // 이미 소비된 투사체거나 잘못된 index
        multiplier = hit.damageMultiplier;

        const Vector3 projPos = pGroup->GetProjectilePosition(projectileIndex, m_stageClockMs);

        // 사거리 sanity 검증 (관대; 정밀 핵검사는 후속).
        if ((projPos - params.origin).LengthXZ() > params.maxRange + k_projectileRangeToleranceXZ)
            return;
        explosionPos = projPos;

        // 직격 대미지: 보고된 타겟이 적 타입일 때만 (진영 검증 + 안전한 캐스팅).
        const EObjectType enemyType =
            (params.casterObjectType == EObjectType::Monster) ? EObjectType::User : EObjectType::Monster;
        if (StageObject* pTargetObj = FindObject(targetId))
        {
            if (pTargetObj->GetObjectType() == enemyType)
                ApplyEffectDamage(*static_cast<ActorObject*>(pTargetObj), params.damageAmount * multiplier, params.casterObjectId, hit.isDuplicate);
        }
    }

    // 폭발: OnHitSkillKey 가 있으면 explosionPos 에 폭발 스킬을 발동한다 (배율 물림, 적중은 서버 판정).
    if (params.onHitSkillKey != 0)
    {
        if (const GameData_Skill* pExplosion = GameDataTable_Skill::FindData(params.onHitSkillKey))
        {
            EffectParams explosionParams = BakeSkillEffectParams(
                *pExplosion, params.casterObjectType, params.casterObjectId, explosionPos, params.dir, params.seed);
            explosionParams.damageAmount *= multiplier;
            SpawnSkillAreaEffect(explosionParams);
        }
    }
}

// 시전 통보를 시전자 주변 AOI 유저들에게 broadcast.
void Stage::BroadcastSkillCastNtf(const ActorObject& caster, int32 skillKey, int64 effectId,
                                  const Vector3& origin, const Vector3& dir, uint32 seed,
                                  float moveDistance)
{
    m_aoiUserScratch.clear();
    ForEachUserInAoi(caster.GetCurSectorX(), caster.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    GameServer::Instance().GetPacketSender().SendSkillCastNtf(m_aoiUserScratch, caster.GetObjectId(), skillKey, effectId,
                              origin.x, origin.y, origin.z, dir.x, dir.z, seed, moveDistance);
}

// 능력 시전 "시작" 통보를 시전자 주변 AOI 유저들에게 broadcast. (몬스터/NPC/엘리트 공용)
void Stage::BroadcastAbilityCastNtf(const ActorObject& caster, int32 skillKey, int64 targetObjectId,
                                    const Vector3& origin, const Vector3& dir, int32 windupMs)
{
    m_aoiUserScratch.clear();
    ForEachUserInAoi(caster.GetCurSectorX(), caster.GetCurSectorZ(),
        [&](int64 accountId) { m_aoiUserScratch.push_back(accountId); });

    GameServer::Instance().GetPacketSender().SendAbilityCastNtf(m_aoiUserScratch, caster.GetObjectId(), skillKey, targetObjectId,
                              origin.x, origin.y, origin.z, dir.x, dir.z, windupMs);
}
