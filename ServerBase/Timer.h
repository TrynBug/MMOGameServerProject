#pragma once

#include "pch.h"

namespace serverbase
{

// 반복 타이머
// 등록된 콜백을 지정한 주기로 호출한다.
// 내부적으로 단일 타이머 스레드를 돌리면서 1초마다 만료 여부를 확인한다.
// 등록, 해제는 thread-safe함
class Timer
{
public:
    Timer()  = default;
    ~Timer() = default;

    Timer(const Timer&)            = delete;
    Timer& operator=(const Timer&) = delete;

public:
    // 타이머 스레드 시작
    void Start();

    // 타이머 스레드 정지 및 모든 타이머 제거
    void Stop();

    // 반복 타이머 등록
    // @intervalMs : 타이머 주기(밀리초)
    // @callback : 호출할 함수
    // @return : 타이머 ID(해제 시 사용)
    int32 Register(int64 intervalMs, std::function<void()> callback);

    // 타이머 해제
    void Unregister(int32 timerId);

private:
    void threadProc();

private:
    // 타이머 1개 데이터
    struct TimerEntry
    {
        int32                    id;
        int64                    intervalMs;  // 반복 주기
        std::function<void()>    callback;
        std::chrono::steady_clock::time_point nextFireTime;  // 다음 실행시간
    };

    std::mutex              m_mutex;
    std::vector<TimerEntry> m_timers;  // 등록된 타이머 목록
    std::atomic<int32>      m_nextId { 1 };
    std::thread             m_thread;  // 타이머 스레드
    std::atomic<bool>       m_bRunning { false };
    std::condition_variable m_cv;
};

} // namespace serverbase
