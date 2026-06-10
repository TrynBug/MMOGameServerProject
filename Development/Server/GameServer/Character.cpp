#include "pch.h"
#include "Character.h"
#include "GameServer.h"
#include "Stage.h"

#include "Generated/GameData_JobBase.h"

#include <cmath>

namespace
{
    // 라디안 -> degree 변환 상수 (스킬 강제이동 yaw 계산용).
    constexpr float k_radToDeg = 57.2957795f;   // 180.0f / PI
}

bool Character::Initialize(const DataStructures::Character& protoData)
{
    if (!ActorObject::Initialize(protoData.character_id(), EObjectType::User))
        return false;

    m_protoData = protoData;

    // proto의 좌표/yaw를 부모 StageObject 멤버에 복사.
    // (런타임은 StageObject 멤버를 진실의 원천으로 사용한다.)
    SetPos(protoData.pos_x(), protoData.pos_y(), protoData.pos_z());
    SetYaw(protoData.yaw());

    // 이동 상태는 정지로 시작(WaypointMover 기본값이 정지).

    // JobBase 게임데이터의 기본스탯을 스탯 컴포넌트에 적용.
    // (향후 레벨/아이템/마스터리/버프는 이어서 각각의 소스가 ApplyStat 한다.)
    if (!applyJobBaseStats())
        return false;

    // 현재 HP/MP 를 최대치로 초기화 (생성 시 풀피).
    // applyJobBaseStats() 이후여야 HpTotal/MpTotal 이 계산되어 있어 최대치가 정해진다.
    FillHp();
    FillMp();

    return true;
}

bool Character::applyJobBaseStats()
{
    const EJob job = static_cast<EJob>(m_protoData.job_id());
    const GameData_JobBase* pJobBase = GameDataTable_JobBase::FindDataByJob(job);
    if (pJobBase == nullptr)
    {
        // JobBase 데이터가 없으면 기본스탯을 적용할 수 없으므로 초기화 실패로 처리한다(false 리턴).
        // 호출자 Character::Initialize 가 false 를 받아 캐릭터 생성을 중단한다.
        LOG_WRITE(LogLevel::Error, std::format("JobBase not found. objectId={} jobId={}", GetObjectId(), m_protoData.job_id()));
        return false;
    }

    // (EStat, value) 쌍 목록을 순회하며 적용. Stat 이 None 인 슬롯은 건너뛴다.
    const int32 statCount = pJobBase->GetStatCount();
    for (int32 i = 0; i < statCount; ++i)
    {
        const EStat stat = pJobBase->GetStat(i);
        if (stat == EStat::None)
            continue;
        m_statComponent.ApplyStat(stat, pJobBase->GetStatValue(i));
    }

    return true;
}

void Character::SyncRuntimeToProto()
{
    // DB 직렬화 직전에 호출되어, 런타임 좌표/yaw를 proto에 반영.
    m_protoData.set_pos_x(GetPosX());
    m_protoData.set_pos_y(GetPosY());
    m_protoData.set_pos_z(GetPosZ());
    m_protoData.set_yaw(GetYaw());
}

void Character::SetDestination(float destX, float destY, float destZ)
{
    // 스킬 강제이동(대시/블링크) 중에는 일반 이동 입력을 무시한다.
    if (m_skillMoving)
        return;

    // 경로 계산/추적은 공유 컴포넌트가 담당. 직선 폴백 시 Warn 로그 출력(클릭 이동이라 빈도 낮음).
    m_mover.SetDestination(*this, destX, destY, destZ, /*logFallback*/ true);
}

void Character::StopAt(float posX, float posY, float posZ, float yaw)
{
    SetPos(posX, posY, posZ);
    SetYaw(yaw);
    m_mover.Stop();
}

void Character::ApplyActionLock(int64 lockMs)
{
    if (lockMs <= 0)
        return;
    if (lockMs > m_actionLockRemainingMs)
        m_actionLockRemainingMs = lockMs;

    // 시전 시작 시 현재 이동을 즉시 정지(시전 위치에 고정). 서버 권위로 멈춰야 클라 화해도 따라온다.
    m_mover.Stop();
}

void Character::Update(int64 deltaMs)
{
    // 시전 액션락 카운트다운. >0 인 동안은 이동하지 않는다(handleMoveIntentReq 가 이동 입력도 무시).
    if (m_actionLockRemainingMs > 0)
    {
        m_actionLockRemainingMs -= deltaMs;
        if (m_actionLockRemainingMs < 0)
            m_actionLockRemainingMs = 0;
    }

    // 스킬 강제이동이 우선한다 (일반 이동과 배타적).
    if (m_skillMoving)
    {
        advanceSkillMove(deltaMs);
        return;
    }

    // 경로 추적은 공유 컴포넌트가 담당. 이동속도는 캐릭터 스탯에서.
    m_mover.Update(*this, deltaMs, static_cast<float>(m_statComponent.Get(EStat::MoveSpdTotal)));
}

void Character::ApplySkillMovement(const Vector3& dir, float distance, int32 durationMs)
{
    // XZ 방향 정규화. 거리/방향이 무의미하면 무시.
    float dx = dir.x;
    float dz = dir.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f || distance <= 0.0f)
        return;
    dx /= len;
    dz /= len;

    // 강제이동은 일반(waypoint) 이동을 중단시킨다.
    m_mover.Stop();

    // 시전 방향으로 즉시 회전 (Unity 호환 yaw).
    SetYaw(std::atan2(dx, dz) * k_radToDeg);

    if (durationMs <= 0)
    {
        // 즉시 블링크: 끝점으로 스냅 + 섹터 즉시 갱신 (이번 tick AOI/visibility 정합).
        SetPos(GetPosX() + dx * distance, GetPosY(), GetPosZ() + dz * distance);
        m_skillMoving = false;
        if (Stage* pStage = GetStage())
            pStage->UpdateObjectSector(this);
        return;
    }

    // 감속 대시: 상태만 설정하고 매 tick Update(advanceSkillMove)에서 전진.
    m_skillMoveStartX     = GetPosX();
    m_skillMoveStartY     = GetPosY();
    m_skillMoveStartZ     = GetPosZ();
    m_skillMoveDirX       = dx;
    m_skillMoveDirZ       = dz;
    m_skillMoveDistance   = distance;
    m_skillMoveDurationMs = durationMs;
    m_skillMoveElapsedMs  = 0;
    m_skillMoving         = true;
}

void Character::advanceSkillMove(int64 deltaMs)
{
    m_skillMoveElapsedMs += deltaMs;

    const float t = (m_skillMoveDurationMs > 0)
        ? static_cast<float>(m_skillMoveElapsedMs) / static_cast<float>(m_skillMoveDurationMs)
        : 1.0f;

    if (t >= 1.0f)
    {
        // 종료: 끝점으로 정확히 스냅 (클라/서버 끝위치 일치).
        SetPos(m_skillMoveStartX + m_skillMoveDirX * m_skillMoveDistance,
               m_skillMoveStartY,
               m_skillMoveStartZ + m_skillMoveDirZ * m_skillMoveDistance);
        m_skillMoving = false;
        return;
    }

    // ease-out quad: f = 1 - (1-t)^2. 클라이언트와 반드시 동일한 공식이어야 한다.
    const float f = 1.0f - (1.0f - t) * (1.0f - t);
    SetPos(m_skillMoveStartX + m_skillMoveDirX * m_skillMoveDistance * f,
           m_skillMoveStartY,
           m_skillMoveStartZ + m_skillMoveDirZ * m_skillMoveDistance * f);
}

void Character::OnStatsChangedByBuff()
{
    // Buff added/removed/stacked changed stats -> resend full stat snapshot to the owner client.
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;
    GameServer* pServer = pStage->GetGameServer();
    if (pServer == nullptr)
        return;
    pServer->GetPacketSender().SendStatUpdateNtf(GetProto().owner_user_id(), *this);
}

void Character::OnHpChangedByBuff()
{
    // DoT/HoT tick changed current HP -> send current HP/MP to the owner client.
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;
    GameServer* pServer = pStage->GetGameServer();
    if (pServer == nullptr)
        return;
    pServer->GetPacketSender().SendHpMpNtf(GetProto().owner_user_id(), GetCurHp(), GetCurMp());
}
