#include "pch.h"
#include "Stage.h"
#include "ActorObject.h"   // ApplyEffectDamage / OnSkillProjectileHit 의 ActorObject 완전타입
#include "GameServer.h"    // SendSkill*Ntf / GenerateObjectId
#include "Skill/EffectShape.h"   // QueryEnemiesInShape 의 EffectShape / Vector3
#include "Skill/EffectParams.h"  // EffectParams 완전타입
#include "Skill/AreaEffect.h"    // AreaEffect 완전타입 (m_skillAreaEffects 조작)
#include "Skill/ProjectileGroup.h"  // ProjectileGroup 완전타입 (m_skillProjectileGroups 조작)
#include "Skill/SkillBake.h"        // BakeSkillEffectParams (폭발 발동 시)
#include "Generated/GameData_Skill.h"  // GameDataTable_Skill::FindData (OnHitSkillKey 조회)

#include <cmath>
#include <random>

// ─────────────────────────────────────────────────────────────
// Stage 의 스킬 효과 서브시스템.
// 범위공격(AreaEffect) / 투사체 그룹(ProjectileGroup) 의 spawn·tick·만료·대미지·시전통보를 담당한다.
// Stage 클래스의 멤버 함수이며, 파일 크기 관리를 위해 Stage.cpp 에서 분리했다.
// ─────────────────────────────────────────────────────────────

namespace
{
    // 투사체 hit 보고 사거리 sanity 검증 여유 (units, X-Z). 정밀 핵검사는 후속.
    constexpr float k_projectileRangeToleranceXZ = 3.0f;
}

// (centerPos 를 중심으로) shape 범위 안의 "적" StageObject 들을 outEnemies 에 채운다. (X-Z 평면)
// 진영 규칙(v1): Monster 시전자는 User(캐릭터)를, 그 외(User 등) 시전자는 Monster 를 대상으로 한다.
void Stage::QueryEnemiesInShape(EObjectType casterType, const Vector3& centerPos, const EffectShape& shape, std::vector<StageObject*>& outEnemies)
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
                if (shape.Contains(centerPos, objPos))
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
}

// 효과(스킬)에 의한 대미지를 target 의 현재 HP 에서 감소시키고, 주변 AOI 유저들에게 SkillDamageNtf 를 broadcast 한다.
void Stage::ApplyEffectDamage(ActorObject& target, double damage, int64 killerObjectId, bool isDuplicate)
{
    // 이미 사망한 대상에는 추가 대미지를 적용하지 않는다 (사망 후 같은 틱의 잔여 투사체 등 차단).
    if (target.IsDead())
        return;

    target.SetCurHp(target.GetCurHp() - damage);

    const double remainingHp = target.GetCurHp();

    // HP 가 0 이하로 떨어졌으면 이번 호출에서 사망 전환 (1회만 true).
    const bool justDied = (remainingHp <= 0.0) && target.MarkDead(killerObjectId);

    GameServer* pServer = GetGameServer();
    if (pServer == nullptr)
        return;

    const int64 targetObjectId = target.GetObjectId();
    ForEachUserInAoi(target.GetCurSectorX(), target.GetCurSectorZ(),
        [&](int64 userId)
        {
            pServer->GetPacketSender().SendSkillDamageNtf(userId, targetObjectId, damage, isDuplicate, remainingHp);
        });

    // 새로 사망했으면 별도 사망 통보 (대미지 외 사인도 있을 수 있어 SkillDamageNtf 와 분리).
    // 시체는 일정 시간(corpse) 유지 후 Stage::updateMonsters 가 디스폰한다.
    if (justDied)
        BroadcastObjectDeathNtf(target, killerObjectId);
}

// 사망한 대상 주변 AOI 유저들에게 사망을 통보(ObjectDeathNtf). 클라 사망 연출용.
void Stage::BroadcastObjectDeathNtf(const ActorObject& actor, int64 killerObjectId)
{
    GameServer* pServer = GetGameServer();
    if (pServer == nullptr)
        return;

    const int64 objectId = actor.GetObjectId();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 userId)
        {
            pServer->GetPacketSender().SendObjectDeathNtf(userId, objectId, killerObjectId);
        });
}

// SkillComponent 가 bake 한 EffectParams + 부채꼴 방향으로 투사체 그룹을 생성/등록하고, 발급된 effectId 를 리턴한다.
int64 Stage::SpawnSkillProjectileGroup(const EffectParams& params, const std::vector<Vector3>& dirs)
{
    EffectParams p = params;

    // effectId 발급 (클라가 hit 보고 시 이 id 로 그룹을 지목한다).
    if (GameServer* pServer = GetGameServer())
        p.effectId = pServer->GenerateObjectId();

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
    GameServer* pServer = GetGameServer();
    if (pServer == nullptr)
        return;

    const int64 casterObjectId = caster.GetObjectId();
    ForEachUserInAoi(caster.GetCurSectorX(), caster.GetCurSectorZ(),
        [&](int64 userId)
        {
            pServer->GetPacketSender().SendSkillCastNtf(userId, casterObjectId, skillKey, effectId,
                                      origin.x, origin.y, origin.z, dir.x, dir.z, seed, moveDistance);
        });
}
