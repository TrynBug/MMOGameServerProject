#pragma once

#include "Types.h"
#include "NetConfig.h"
#include "PacketPool.h"

#include <winsock2.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace netlib
{

// 네트워크 인프라
// IOCP, Worker 스레드, PacketPool 등을 관리한다.
// 하나의 프로세스에 보통 IoContext 한 개를 두고, 여러 NetServer, NetClient가 이 IoContext를 공유해서 쓴다.
class IoContext
{
public:
    IoContext();
    ~IoContext();

    IoContext(const IoContext&)            = delete;
    IoContext& operator=(const IoContext&) = delete;

    bool Initialize(const IoContextConfig& config);
    void Shutdown();

    bool IsRunning() const { return m_bRunning.load(); }

    HANDLE      GetIocpHandle() const { return m_hIocp; }
    PacketPool& GetPacketPool()       { return m_packetPool; }

    const IoContextConfig& GetConfig() const { return m_config; }

    // 소켓을 IOCP에 등록한다
    bool RegisterSocket(SOCKET socket, ULONG_PTR completionKey);

    // IOCP 메시지큐에 Worker 스레드에서 실행할 함수 입력
    void PostMsg(std::function<void()> fn);

    // deliverAt(절대 시각)에 fn 을 실행하도록 예약한다. (네트워크 지연 시뮬레이션용)
    // 전용 스케줄러 스레드 1개에서 deliverAt 순서로 직렬 실행한다. 순서 보존을 위한 deliverAt 계산은
    // 호출측(Session)이 세션별 단조 cursor 로 직접 한다.
    void ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn);

private:
    void workerThreadProc();
    void delaySchedulerProc();

    // 지연 스케줄러 항목. deliverAt 이 이른 것이 우선, 같은 시각은 seq 로 FIFO(순서 보존).
    struct DelayItem
    {
        std::chrono::steady_clock::time_point deliverAt;
        uint64                                seq = 0;
        std::function<void()>                 fn;
    };
    struct DelayItemCompare
    {
        bool operator()(const DelayItem& a, const DelayItem& b) const
        {
            if (a.deliverAt != b.deliverAt)
                return a.deliverAt > b.deliverAt;   // 이른 시각이 top
            return a.seq > b.seq;                   // 동시각이면 먼저 들어온 것이 top
        }
    };

private:
    IoContextConfig          m_config;
    HANDLE                   m_hIocp        = nullptr;

    std::vector<std::thread> m_workerThreads;
    std::atomic<bool>        m_bRunning     { false };
    std::atomic<bool>        m_bWsaStarted  { false };

    PacketPool               m_packetPool;

    // ── 지연 스케줄러 (네트워크 지연 시뮬레이션) ──
    std::priority_queue<DelayItem, std::vector<DelayItem>, DelayItemCompare> m_delayQueue;
    std::mutex               m_delayMutex;
    std::condition_variable  m_delayCv;
    std::thread              m_delayThread;
    uint64                   m_delaySeq     = 0;
};

using IoContextPtr = std::shared_ptr<IoContext>;
using IoContextWPtr = std::weak_ptr<IoContext>;
using IContextUPtr = std::unique_ptr<IoContext>;

} // namespace netlib
