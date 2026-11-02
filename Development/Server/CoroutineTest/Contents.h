#pragma once

#include "DB.h"
#include "Example.h"

enum class JobType
{
    PrintMessage,
    LoadGold,
    MAX,
    ResumeCoroutine // 내부 재개 작업. main의 무작위 선택 범위(0~MAX-1)에는 포함되지 않는다.
};

class Contents;
class DbWorkerPool;

// 전용 스레드는 없으며 Dispatch와 후속 처리는 IOCP worker에서 실행된다.
// 여러 worker에서 동시에 호출될 수 있으므로 이 예제는 변경 가능한 게임 상태를 보관하지 않는다.
class Contents : public std::enable_shared_from_this<Contents>
{
public:
    explicit Contents(std::shared_ptr<DbWorkerPool> db);
    void Dispatch(JobType type);
private:
    void handlePrintMessage();
    void handleLoadGold();
    static DetachedTask loadGoldAsync(std::shared_ptr<Contents> spContents, std::shared_ptr<ResumeExecutor> executor);

    std::shared_ptr<DbWorkerPool> m_db;
};
