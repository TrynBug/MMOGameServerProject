#pragma once

#include "pch.h"
#include "Contents.h"

namespace serverbase
{

// 컨텐츠 스레드
// 소유한 Contents객체들을 주기적으로 Update한다. 하나의 Contents 객체는 객체가 소유된 스레드(단일스레드)에서만 업데이트된다.
// ServerBase는 여러개의 ContentsThread를 생성한다.
// 게임서버 등은 Contents객체(예: Stage)를 생성하여 [컨텐츠객체ID % 컨텐츠스레드수] 등의 연산으로 배정할 ContentsThread를 선택하여 컨텐츠를 배정한다.
class ContentsThread
{
public:
    // @updateIntervalMs : Contents::Update 호출 주기(ms)
    explicit ContentsThread(int64 updateIntervalMs = 50);
    ~ContentsThread();

    ContentsThread(const ContentsThread&) = delete;
    ContentsThread& operator=(const ContentsThread&) = delete;

public:
    // 스레드 시작
    void Start();

    // 스레드 종료 (모든 Contents::OnStop 호출 후 스레드 join)
    void Stop();

    // Contents 등록. 스레드 실행 중에도 안전하게 추가 가능.
    void AddContents(ContentsPtr spContents);

    // Contents 제거. 스레드 실행 중에도 안전하게 제거 가능.
    void RemoveContents(ContentsPtr spContents);

    int32 GetContentsCount() const;

    bool IsRunning() const { return m_bRunning.load(); }

private:
    void threadProc();

private:
    int64 m_updateIntervalMs;
    std::thread m_thread;
    std::atomic<bool> m_bRunning { false };

    mutable std::mutex m_contentsMutex;
    std::vector<ContentsPtr> m_contents;

    // thread-safe하게 컨텐츠를 추가/제거 하기위한 대기 큐
    std::mutex m_pendingMutex;
    std::vector<ContentsPtr> m_pendingAdd;
    std::vector<ContentsPtr> m_pendingRemove;
};

using ContentsThreadPtr = std::shared_ptr<ContentsThread>;
using ContentsThreadWPtr = std::weak_ptr<ContentsThread>;
using ContentsThreadUPtr = std::unique_ptr<ContentsThread>;

} // namespace serverbase
