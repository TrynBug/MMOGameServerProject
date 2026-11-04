#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <coroutine>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 로그출력
void Trace(const std::string& message);
// 조건체크
void Check(bool condition, const char* message);

// 이 컴포넌트는 코루틴이 중지됐을 때 어떤 스레드에서 재개되어야 하는지 결정할때 사용된다.
class IResumeExecutor
{
public:
    virtual ~IResumeExecutor() = default;
    virtual void Post(std::function<void()> task) = 0;
};

class Iocp;
class Contents;
enum class JobType;

// 코루틴 재개 요청을 IOCP worker 스레드에 전달하는 컴포넌트다.
class IocpResumeExecutor : public IResumeExecutor
{
public:
    explicit IocpResumeExecutor(Iocp& owner) : m_owner(owner) {}
    void Post(std::function<void()> task) override;
private:
    Iocp& m_owner;
};

// IOCP 포트 하나와 그 포트에서 작업을 꺼내 실행하는 worker들을 소유한다.
// 내부 job 객체를 생성하고 completion packet으로 전달한 뒤 처리 완료 시 해제한다.
class Iocp
{
public:
    static std::shared_ptr<Iocp> Instance();

private:
    Iocp() = default;
    Iocp(const Iocp&) = delete;
    Iocp& operator=(const Iocp&) = delete;

public:
    ~Iocp();

public:
    void Start(int workerCount);
    void Stop();

    std::shared_ptr<IResumeExecutor> GetResumeExecutor();

    void PostJob(JobType type, std::shared_ptr<Contents> contents);
    void Post(std::function<void()> task);

private:

    // job
    struct IocpJob
    {
        JobType type;
        std::shared_ptr<Contents> contents;
        std::function<void()> resumeTask;
    };

    
    void postJob(std::unique_ptr<IocpJob> job);

    void run();

private:
    HANDLE m_hIOCP = nullptr;
    std::vector<std::thread> m_workers;
    IocpResumeExecutor m_resumeExecutor { *this };
};
