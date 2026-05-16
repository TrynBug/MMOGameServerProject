#pragma once

#include "pch.h"

// Stage 종류
enum class StageType : uint8
{
    Unknown      = 0,
    OpenField    = 1,   // 오픈필드 (게임서버당 1개, 전용 컨텐츠 스레드)
    PublicDungeon = 2,  // 공용던전 (시간 기반 입장)
    UserDungeon  = 3,   // 유저던전 (1명이 생성)
};


// Stage 베이스 클래스
// 캐릭터가 돌아다닐 수 있는 1개의 필드를 나타낸다.
// 단일 컨텐츠 스레드에서만 업데이트되므로 내부 로직은 단일 스레드로 작성한다.
// 자세한 설계는 서버구조개요.md의 'Stage 클래스' 절 참조.
//
// 2단계: 베이스 골격만 제공 (StageId, StageType, Update 훅).
// 향후 단계에서 추가될 것:
//   - 시스템 메시지 큐 (유저입장/연결끊김/서버종료 등)
//   - 유저 객체 컨테이너
//   - 섹터 분할 (AOI)
class Stage : public serverbase::Contents
{
public:
    Stage(int64 stageId, StageType stageType);
    ~Stage() override = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

public:
    int64     GetStageId()   const { return m_stageId; }
    StageType GetStageType() const { return m_stageType; }

protected:
    // serverbase::Contents 훅
    void OnStart()              override;
    void OnUpdate(int64 deltaMs) override;
    void OnStop()               override;

    // Stage 파생 클래스가 매 tick마다 처리할 로직을 override한다.
    // OnUpdate 안에서 호출된다.
    virtual void OnStageUpdate(int64 deltaMs) {}

private:
    int64     m_stageId   = 0;
    StageType m_stageType = StageType::Unknown;

    // 5초 주기 heartbeat 로그용 누적 시간
    int64     m_heartbeatAccumMs = 0;
};

using StagePtr  = std::shared_ptr<Stage>;
using StageWPtr = std::weak_ptr<Stage>;
using StageUPtr = std::unique_ptr<Stage>;
