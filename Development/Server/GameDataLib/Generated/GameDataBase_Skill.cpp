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
    std::getline(ss, field, ','); pData->IsUserCastable = StringToBool(field);
    std::getline(ss, field, ','); pData->NextSkillKey = std::stoll(field);
    std::getline(ss, field, ','); pData->TriggerDelay = std::stod(field);
    std::getline(ss, field, ','); pData->Job = static_cast<EJob>(std::stoi(field));
    std::getline(ss, field, ','); pData->Category = static_cast<ESkillCategory>(std::stoi(field));
    std::getline(ss, field, ','); pData->CoolTime = std::stod(field);
    std::getline(ss, field, ','); pData->ManaCost = std::stod(field);
    std::getline(ss, field, ','); pData->CastTime = std::stod(field);
    std::getline(ss, field, ','); pData->NeedsTargeting = StringToBool(field);
    std::getline(ss, field, ','); pData->EffectType = static_cast<EEffectType>(std::stoi(field));
    std::getline(ss, field, ','); pData->Damage = std::stod(field);
    std::getline(ss, field, ','); pData->OriginType = static_cast<EOriginType>(std::stoi(field));
    std::getline(ss, field, ','); pData->RangeShape = static_cast<ERangeShape>(std::stoi(field));
    std::getline(ss, field, ','); pData->RangeX = std::stod(field);
    std::getline(ss, field, ','); pData->RangeY = std::stod(field);
    std::getline(ss, field, ','); pData->IntervalSec = std::stod(field);
    std::getline(ss, field, ','); pData->FirstHitDelay = std::stod(field);
    std::getline(ss, field, ','); pData->DurationSec = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileCount = std::stoll(field);
    std::getline(ss, field, ','); pData->ProjectileSpreadAngle = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileSpeed = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileRange = std::stod(field);
    std::getline(ss, field, ','); pData->ProjectileMaxHitPerTarget = std::stoll(field);
    std::getline(ss, field, ','); pData->MoveDistance = std::stod(field);
    std::getline(ss, field, ','); pData->MoveTime = std::stod(field);
    std::getline(ss, field, ','); pData->MoveFastPhaseRatio = std::stod(field);
    std::getline(ss, field, ','); pData->MoveFastTimeRatio = std::stod(field);
    std::getline(ss, field, ','); pData->MaxTargetCount = std::stoll(field);
    std::getline(ss, field, ','); pData->KnockbackDistance = std::stod(field);
    std::getline(ss, field, ','); pData->BuffKey = std::stoll(field);
    std::getline(ss, field, ','); pData->SlowOnHitKey = std::stoll(field);

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
