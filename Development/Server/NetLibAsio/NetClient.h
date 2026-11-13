#pragma once

#include "../NetLibCommon/INetBase.h"
#include "../NetLibCommon/NetConfig.h"

namespace netlib
{

class IoContext;

// 원격 TCP 서버로 접속하고, 설정에 따라 재접속을 예약한다.
class NetClient : public INetBase
{
public:
    explicit NetClient(IoContext* pIoContext);
    ~NetClient() override;

    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    bool Initialize(const NetClientConfig& config);
    // 접속을 영구 중지하고 세션 종료 콜백을 마친다. 관리 스레드에서 호출한다.
    void Shutdown();
    // 등록한 객체는 Shutdown() 또는 IoContext::Shutdown()이 끝날 때까지 호출자가 유지한다.
    void SetEventHandler(INetEventHandler* handler);

    // 연결 시도를 요청한다. 실제 연결 성공은 OnConnect로 통지한다.
    bool Connect(const std::string& ip, uint16 port);
    // 현재 세션을 닫고 진행 중인 접속과 재접속 예약을 취소한다.
    void Disconnect();
    std::shared_ptr<ISession> GetSession();
    bool IsConnected();

    INetEventHandler* GetEventHandler() override;
    PacketPool& GetPacketPool() override;
    int32 GetMaxPacketSize() const override;
    void OnSessionDisconnected(std::shared_ptr<ISession> spSession) override;
    void ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn) override;

private:
    struct State;
    std::shared_ptr<State> m_state; // 접속 완료 및 재접속 타이머 핸들러와 공유하는 상태
};

using NetClientPtr = std::shared_ptr<NetClient>;
using NetClientWPtr = std::weak_ptr<NetClient>;
using NetClientUPtr = std::unique_ptr<NetClient>;

} // namespace netlib
