#pragma once

#include "pch.h"
#include "Stage.h"

// 오픈필드 클래스
// 게임서버당 1개 존재. 전용 컨텐츠 스레드(인덱스 0번)에서 처리된다.
// 자세한 설계는 서버구조개요.md의 '오픈필드 클래스' 절 참조.
class OpenField : public Stage
{
public:
    explicit OpenField(int64 stageId);
    ~OpenField() override = default;

    OpenField(const OpenField&) = delete;
    OpenField& operator=(const OpenField&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;
};

using OpenFieldPtr = std::shared_ptr<OpenField>;
