#include "pch.h"
#include "IoContext.h"
#include "ISession.h"
#include "OverlappedEx.h"
#include "NetLibStats.h"

#include "Session.h"

namespace netlib
{

IoContext::IoContext()
{
}

IoContext::~IoContext()
{
    Shutdown();
}

// 초기화
bool IoContext::Initialize(const IoContextConfig& config)
{
    if (m_bRunning.load())
    {
        return false;
    }

    m_config = config;

    // WSAStartup
    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        return false;
    }

    m_bWsaStarted.store(true);

    // IOCP 생성
    DWORD numConcurrentThread = 0;
    if (m_config.numConcurrentThread > 0)
    {
        numConcurrentThread = static_cast<DWORD>(m_config.numConcurrentThread);
    }

    m_hIocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, numConcurrentThread);
    if (m_hIocp == nullptr)
    {
        ::WSACleanup();
        m_bWsaStarted.store(false);
        return false;
    }

    // 패킷 풀 초기화
    m_packetPool.Initialize(m_config.initPacketSize, m_config.maxPacketSize);

    // Worker 스레드 개수 계산
    int32 numWorkers = m_config.numWorkerThread;
    if (numWorkers <= 0)
    {
        numWorkers = static_cast<int32>(std::thread::hardware_concurrency()) * 2;
        if (numWorkers <= 0)
        {
            numWorkers = 4;
        }
    }

    // worker 스레드 시작
    m_bRunning.store(true);
    for (int32 i = 0; i < numWorkers; ++i)
    {
        m_workerThreads.emplace_back(&IoContext::workerThreadProc, this);
    }

    return true;
}

// 종료
void IoContext::Shutdown()
{
    if (!m_bRunning.exchange(false))
    {
        return;
    }

    // Worker 스레드에 종료 신호 보내기
    if (m_hIocp != nullptr)
    {
        for (size_t i = 0; i < m_workerThreads.size(); ++i)
        {
            ::PostQueuedCompletionStatus(m_hIocp, 0, 0, nullptr);
        }
    }

    // worker 스레드 종료 대기
    for (auto& th : m_workerThreads)
    {
        if (th.joinable())
        {
            th.join();
        }
    }
    m_workerThreads.clear();

    if (m_hIocp != nullptr)
    {
        ::CloseHandle(m_hIocp);
        m_hIocp = nullptr;
    }

    if (m_bWsaStarted.exchange(false))
    {
        ::WSACleanup();
    }
}

// 소켓을 IOCP에 등록
bool IoContext::RegisterSocket(SOCKET socket, ULONG_PTR completionKey)
{
    if (m_hIocp == nullptr)
    {
        return false;
    }

    const HANDLE ret = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), m_hIocp, completionKey, 0);
    return (ret != nullptr);
}

void IoContext::PostMsg(std::function<void()> fn)
{
    // POST_OVERLAPPED을 힙에 생성하고 IOCP 메시지큐에 입력한다.
    // Worker 스레드가 꺼내서 fn을 실행한 후 delete한다.
    auto* pPost = new POST_OVERLAPPED();
    pPost->fn = std::move(fn);
    ::PostQueuedCompletionStatus(m_hIocp, 0, 0, reinterpret_cast<OVERLAPPED*>(pPost));
}

// worker 스레드
void IoContext::workerThreadProc()
{
    while (m_bRunning.load())
    {
        DWORD           bytesTransferred = 0;
        ULONG_PTR       completionKey    = 0;
        OVERLAPPED_EX*  pOverlapped      = nullptr;

        // GQCS
        const BOOL ret = ::GetQueuedCompletionStatus(m_hIocp, &bytesTransferred, &completionKey, reinterpret_cast<OVERLAPPED**>(&pOverlapped), INFINITE);

        // 스레드 종료신호를 받음
        if (pOverlapped == nullptr && completionKey == 0)
        {
            break;
        }

        // IO_TYPE::Post 임의 함수 실행
        if (pOverlapped->ioType == IO_TYPE::Post) // (POST_OVERLAPPED 와 OVERLAPPED_EX 의 ioType 멤버 위치가 같아서 이렇게 검사해도됨)
        {
            POST_OVERLAPPED* pPostOverlapped = reinterpret_cast<POST_OVERLAPPED*>(pOverlapped);
            pPostOverlapped->fn();
            delete pPostOverlapped;
            continue;
        }

        // Session 얻기
		SessionPtr spSession = static_pointer_cast<Session>(pOverlapped->spSession);

        // 오류발생
        if (ret == FALSE)
        {
            if (pOverlapped->ioType == IO_TYPE::Recv)
            {
                NetLibStats::Inc(StatCounter::RecvKnownFailed);
            }
            else if (pOverlapped->ioType == IO_TYPE::Send)
            {
                NetLibStats::Inc(StatCounter::SendKnownFailed);
            }
            else if (pOverlapped->ioType == IO_TYPE::Connect)
            {
                if (spSession != nullptr)
                {
                    spSession->OnConnectCompleted(false);
                }
                continue;
            }

            NetLibStats::Inc(StatCounter::AbnormalDisconnect);

            if (spSession != nullptr)
            {
                spSession->CloseSocket();
            }
            continue;
        }

        // 연결 종료됨
        if (bytesTransferred == 0 && pOverlapped->ioType == IO_TYPE::Recv)
        {
            NetLibStats::Inc(StatCounter::GracefulDisconnect);
            if (spSession != nullptr)
            {
                spSession->CloseSocket();
            }
            continue;
        }

        // 정상 완료통지 처리
        switch (pOverlapped->ioType)
        {
            case IO_TYPE::Recv:
                if (spSession != nullptr)
                {
                    spSession->OnRecvCompleted(bytesTransferred);
                }
                break;
            case IO_TYPE::Send:
                if (spSession != nullptr)
                {
                    spSession->OnSendCompleted(bytesTransferred);
                }
                break;
            case IO_TYPE::Connect:
                if (spSession != nullptr)
                {
                    spSession->OnConnectCompleted(ret == TRUE);
                }
                break;
            default:
                break;
        }
    }
}

} // namespace netlib
