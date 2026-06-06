// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_Skill.h"
#include "GameData_Skill.h"

const GameData_Skill* GameDataTableBase_Skill::FindData(int64_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_Skill::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Skill* pData = new GameData_Skill;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoll(field);
    std::getline(ss, field, ','); pData->Name = field;
    std::getline(ss, field, ','); pData->Job = static_cast<EJob>(std::stoi(field));
    std::getline(ss, field, ','); pData->CastClass = static_cast<ESkillCastClass>(std::stoi(field));
    std::getline(ss, field, ','); pData->IsPrimaryEligible = StringToBool(field);
    std::getline(ss, field, ','); pData->OnHitSkillKey = std::stoll(field);
    std::getline(ss, field, ','); pData->NextSkillKey = std::stoll(field);
    std::getline(ss, field, ','); pData->NextTriggerTiming = static_cast<ENextSkillTiming>(std::stoi(field));
    std::getline(ss, field, ','); pData->NextTriggerDelayMs = std::stoll(field);
    std::getline(ss, field, ','); pData->NextOrigin = static_cast<ENextSkillOrigin>(std::stoi(field));
    std::getline(ss, field, ','); pData->CasterFrontDistance = std::stod(field);
    std::getline(ss, field, ','); pData->CooldownMs = std::stoll(field);
    std::getline(ss, field, ','); pData->ManaCost = std::stod(field);
    std::getline(ss, field, ','); pData->CastDelayMs = std::stoll(field);
    std::getline(ss, field, ','); pData->ActionLockMs = std::stoll(field);
    std::getline(ss, field, ','); pData->Rotation = StringToBool(field);
    std::getline(ss, field, ','); pData->Targeting = static_cast<ETargetingMode>(std::stoi(field));
    std::getline(ss, field, ','); pData->Placement = static_cast<ESkillPlacement>(std::stoi(field));
    std::getline(ss, field, ','); pData->EffectMotion = static_cast<ESkillEffectMotion>(std::stoi(field));
    std::getline(ss, field, ','); pData->EffectDamage = static_cast<ESkillEffectDamage>(std::stoi(field));
    std::getline(ss, field, ','); pData->EffectShape = static_cast<ESkillEffectShape>(std::stoi(field));
    std::getline(ss, field, ','); pData->DamageCoeff = std::stod(field);
    std::getline(ss, field, ','); pData->Radius = std::stod(field);
    std::getline(ss, field, ','); pData->ObbWidth = std::stod(field);
    std::getline(ss, field, ','); pData->ObbLength = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileSpeed = std::stod(field);
    std::getline(ss, field, ','); pData->MaxRange = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileCount = std::stoll(field);
    std::getline(ss, field, ','); pData->FanAngleDeg = std::stod(field);
    std::getline(ss, field, ','); pData->FirstTickDelayMs = std::stoll(field);
    std::getline(ss, field, ','); pData->TickIntervalMs = std::stoll(field);
    std::getline(ss, field, ','); pData->LifetimeMs = std::stoll(field);
    std::getline(ss, field, ','); pData->MoveDistance = std::stod(field);
    std::getline(ss, field, ','); pData->MoveDurationMs = std::stoll(field);
    std::getline(ss, field, ','); pData->ScatterCount = std::stoll(field);
    std::getline(ss, field, ','); pData->ScatterInnerRadius = std::stod(field);
    std::getline(ss, field, ','); pData->ScatterOuterRadius = std::stod(field);

    if (pData->Key <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid key value. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    if (sm_dataMap.contains(pData->Key))
    {
        LOG_WRITE(LogLevel::Error, std::format("Duplicate table key. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    if (false == pData->Initialize())
    {
        LOG_WRITE(LogLevel::Error, std::format("Failed to initialize data. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    sm_dataMap.insert(std::pair(pData->Key, pData));

    return OnAddData(pData);
}
