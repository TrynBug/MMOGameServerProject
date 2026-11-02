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

void Trace(const std::string& message);
void Check(bool condition, const char* message);

// 코루틴의 후속 코드를 실행할 스레드에 작업을 전달하는 인터페이스다.
// DB worker는 Post로 재개 작업을 등록하고, 대상 스레드가 나중에 그 작업을 실행한다.
class ResumeExecutor
{
public:
    virtual ~ResumeExecutor() = default;
    virtual void Post(std::function<void()> task) = 0;
};

class IocpExecutor;
class Contents;
enum class JobType;

// 코루틴 재개 요청을 소유자의 IOCP 작업 등록 함수로 전달하는 컴포넌트다.
class IocpResumeExecutor : public ResumeExecutor
{
public:
    explicit IocpResumeExecutor(IocpExecutor& owner) : m_owner(owner) {}
    void Post(std::function<void()> task) override;
private:
    IocpExecutor& m_owner;
};

// IOCP 포트 하나와 그 포트에서 작업을 꺼내 실행하는 worker들을 소유한다.
// 내부 job 객체를 생성하고 completion packet으로 전달한 뒤 처리 완료 시 해제한다.
class IocpExecutor
{
public:
    static std::shared_ptr<IocpExecutor> Instance();
    ~IocpExecutor();
    void Start(int workerCount);
    void PostJob(JobType type, std::shared_ptr<Contents> contents);
    std::shared_ptr<ResumeExecutor> GetResumeExecutor();
    void Post(std::function<void()> task);
    void Stop();
private:
    // job 자체는 IOCP만 소유한다. 컨텐츠는 대기/처리 중 shared_ptr로 유지한다.
    struct IocpJob
    {
        JobType type;
        std::shared_ptr<Contents> contents;
        std::function<void()> resumeTask;
    };

    IocpExecutor() = default;
    IocpExecutor(const IocpExecutor&) = delete;
    IocpExecutor& operator=(const IocpExecutor&) = delete;
    void postJob(std::unique_ptr<IocpJob> job);
    void run();
    HANDLE m_port = nullptr;
    std::vector<std::thread> m_workers;
    IocpResumeExecutor m_resumeExecutor { *this };
};
