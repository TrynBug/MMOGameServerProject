#include "DB.h"
#include "IOCP.h"
#include "Contents.h"
#include <iostream>
#include <stdexcept>
#include <syncstream>

void Trace(const std::string& message)
{
	// 로그 출력
    std::osyncstream(std::cout) << "[thread " << GetCurrentThreadId() << "] " << message << '\n';
}

void Check(bool condition, const char* message)
{
    // 조건이 false면 예외를 던진다.
    if (!condition)
        throw std::runtime_error(message);
}

void IocpResumeExecutor::Post(std::function<void()> task)
{
    // [#코루틴 16 | DB worker 스레드] IOCP에 코루틴 재개 작업을 삽입한다.
    m_owner.Post(std::move(task));
}

// singleton
std::shared_ptr<Iocp> Iocp::Instance()
{
    static std::shared_ptr<Iocp> instance(new Iocp());
    return instance;
}

Iocp::~Iocp()
{
    Stop();
}

void Iocp::Start(int workerCount)
{
    Check(m_hIOCP == nullptr, "IOCP already started");

    // completion port 생성
    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, workerCount);
    Check(m_hIOCP != nullptr, "CreateIoCompletionPort failed");

    // worker 스레드 생성
    for (int index = 0; index < workerCount; ++index)
        m_workers.emplace_back(&Iocp::run, this);
}

// IOCP에 job 등록
void Iocp::PostJob(JobType type, std::shared_ptr<Contents> contents)
{
    std::unique_ptr<IocpJob> job = std::make_unique<IocpJob>(IocpJob{ type, std::move(contents), {} });
    postJob(std::move(job));
}

std::shared_ptr<IResumeExecutor> Iocp::GetResumeExecutor()
{
    return std::shared_ptr<IResumeExecutor>(Instance(), &m_resumeExecutor); // m_resumeExecutor 의 수명을 Iocp와 공유하도록 함
}

// IOCP에 코루틴 재개 job 등록
void Iocp::Post(std::function<void()> task)
{
    std::unique_ptr<IocpJob> job = std::make_unique<IocpJob>(IocpJob{ JobType::ResumeCoroutine, nullptr, std::move(task) });
    postJob(std::move(job));
}

void Iocp::Stop()
{
    if (m_hIOCP == nullptr)
        return;

    // 완료통지 큐에 key=0인 완료통지를 삽입하여 worker 스레드가 종료되도록 한다.
    for (size_t index = 0; index < m_workers.size(); ++index)
        PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);

    for (auto& worker : m_workers)
        worker.join();

    m_workers.clear();
    CloseHandle(m_hIOCP);
    m_hIOCP = nullptr;
}

void Iocp::postJob(std::unique_ptr<IocpJob> job)
{
    // job을 완료통지 큐에 등록
    BOOL result = PostQueuedCompletionStatus(m_hIOCP, 0, reinterpret_cast<ULONG_PTR>(job.get()), nullptr);

    Check(result != FALSE, "PostQueuedCompletionStatus failed");

    job.release();
}

// IOCP worker 스레드 루틴
void Iocp::run()
{
    while (true)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        
        // GQCS
		BOOL result = GetQueuedCompletionStatus(m_hIOCP, &bytes, &key, &overlapped, INFINITE);
        Check(result != FALSE, "GetQueuedCompletionStatus failed");

        if (key == 0)
            return;

        // 큐에서 job의 소유권을 회수한다. 이 반복의 처리가 끝나면 job을 자동 해제한다.
        std::unique_ptr<IocpJob> job(reinterpret_cast<IocpJob*>(key));

        if (job->type == JobType::ResumeCoroutine)
        {
            // [#코루틴 17 | IOCP worker 스레드] 
            // 이 job은 [#코루틴 15] 에서 IOCP 완료통지 큐에 등록한 job 이다.
            // 코루틴 재개 함수를 호출한다.
            job->resumeTask();

            // [#코루틴 25 | IOCP worker 스레드] 코루틴이 완료되었고 코루틴 프레임도 정리되었다.
        }
        else
        {
            // 일반 job 처리 루틴
            job->contents->Dispatch(job->type);
        }
    }
}


