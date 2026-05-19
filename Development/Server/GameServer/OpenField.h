#pragma once

#include "pch.h"
#include "Stage.h"

// 오픈필드 클래스
// 게임서버당 1개 존재. 전용 컨텐츠 스레드(인덱스 0번)에서 처리된다.
// 자세한 설계는 서버구조개요.md의 '오픈필드 클래스' 절 참조.
//
// 현재 GameServer가 OpenField를 생성하지는 않는다. 추후 필요해질 때 부활시킬 예정.
class OpenField : public Stage
{
public:
    // GameData_Stage 키로 생성. 해당 데이터의 stageType/world bounds/sectorSize를 사용.
    explicit OpenField(int64 stageId);
    ~OpenField() override = default;

    OpenField(const OpenField&) = delete;
    OpenField& operator=(const OpenField&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;

private:
    // 위임 생성자용 private 생성자. StageGridParams를 1회만 평가하기 위해 사용.
    OpenField(int64 stageId, const StageGridParams& params);
};

using OpenFieldPtr = std::shared_ptr<OpenField>;
