#pragma once

#include "../NetLibCommon/INetBase.h"
#include "../NetLibCommon/NetConfig.h"

namespace netlib
{

class IoContext;

// TCP 리스너와 수락한 세션을 관리한다.
class NetServer : public INetBase
{
public:
    explicit NetServer(IoContext* pIoContext);
    ~NetServer() override;

    NetServer(const NetServer&) = delete;
    NetServer& operator=(const NetServer&) = delete;

    bool Initialize(const NetServerConfig& config);
    bool StartAccept();
    // 새 연결 수락을 중지한다. 기존 세션은 Shutdown()에서 함께 닫을 수 있다.
    void StopAccept();
    // 리스너와 세션을 영구 종료하고 세션 종료 콜백을 마친다. 관리 스레드에서 호출한다.
    void Shutdown();

    // 등록한 객체는 Shutdown() 또는 IoContext::Shutdown()이 끝날 때까지 호출자가 유지한다.
    void SetEventHandler(INetEventHandler* handler);
    std::shared_ptr<ISession> FindSession(int64 sessionId);
    void RemoveSession(int64 sessionId);
    int32 GetSessionCount();

    INetEventHandler* GetEventHandler() override;
    PacketPool& GetPacketPool() override;
    int32 GetMaxPacketSize() const override;
    void OnSessionDisconnected(std::shared_ptr<ISession> spSession) override;
    void ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn) override;

private:
    struct State;
    std::shared_ptr<State> m_state; // 리스너와 세션 상태를 accept 완료 핸들러와 공유한다.
};

using NetServerPtr = std::shared_ptr<NetServer>;
using NetServerWPtr = std::weak_ptr<NetServer>;
using NetServerUPtr = std::unique_ptr<NetServer>;

} // namespace netlib
