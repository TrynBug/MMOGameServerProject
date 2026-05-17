#pragma once

#include "Types.h"
#include "NetConfig.h"
#include "PacketPool.h"

#include <winsock2.h>
#include <atomic>
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

private:
    void workerThreadProc();

private:
    IoContextConfig          m_config;
    HANDLE                   m_hIocp        = nullptr;

    std::vector<std::thread> m_workerThreads;
    std::atomic<bool>        m_bRunning     { false };
    std::atomic<bool>        m_bWsaStarted  { false };

    PacketPool               m_packetPool;
};

using IoContextPtr = std::shared_ptr<IoContext>;
using IoContextWPtr = std::weak_ptr<IoContext>;
using IContextUPtr = std::unique_ptr<IoContext>;

} // namespace netlib
