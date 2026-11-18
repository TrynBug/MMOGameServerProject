#pragma once

#include "../NetLibCommon/NetConfig.h"
#include "../NetLibCommon/PacketPool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace boost::asio { class io_context; }

namespace netlib
{


// 네트워크 인프라
// boost::asio 기반 워커스레드, 패킷 풀, 이벤트 루프를 관리한다.
// 하나의 프로세스에 IoContext 한 개를 두고, 여러 NetServer, NetClient가 이 IoContext를 공유해서 쓴다.
class IoContext
{
public:
    IoContext();
    ~IoContext();

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

public:
    bool Initialize(const IoContextConfig& config);

    // 등록된 리스너, 클라이언트를 닫고, 워커스레드를 종료하고 기다린다.
    // 주의: 워커스레드가 아닌 다른 스레드에서 호출해야함
    void Shutdown();

    bool IsRunning() const { return m_bRunning.load(); }
    PacketPool& GetPacketPool() { return m_packetPool; }
    const IoContextConfig& GetConfig() const { return m_config; }

    // 워커스레드에서 실행할 작업 등록
    void PostMsg(std::function<void()> fn); 

    // 워커스레드에서 실행할 예약 작업 등록
    void ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn);

private:
    friend class NetClient;
    friend class NetServer;

    // Shutdown 될때 실행할 작업을 등록한다.
    bool registerShutdownCallback(const std::shared_ptr<std::function<void()>>& spShutdown);

    boost::asio::io_context& getContext();

private:
    struct Impl;

    IoContextConfig      m_config;             // config
    PacketPool          m_packetPool;          // 이 컨텍스트의 세션들이 공유하는 패킷 풀

    std::mutex          m_mutex;               // 초기화,종료 작업 등에서 m_impl을 보호
    std::unique_ptr<Impl> m_impl;               // Asio 이벤트 루프, 워커, 예약 작업의 내부 상태

    std::condition_variable m_shutdownCv;      // Shutdown이 완료되기를 기다릴때 쓰는 cv
    std::atomic<bool>   m_bRunning{ false };   // 실행중 여부
};

using IoContextPtr = std::shared_ptr<IoContext>;
using IoContextWPtr = std::weak_ptr<IoContext>;
using IContextUPtr = std::unique_ptr<IoContext>;

} // namespace netlib
