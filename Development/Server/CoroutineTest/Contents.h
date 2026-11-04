#pragma once

#include "DB.h"
#include "IOCP.h"

enum class JobType
{
    PrintMessage,
    LoadGold,
    MAX,
    ResumeCoroutine
};

class Contents;
class DbWorkerPool;

// 컨텐츠 로직 구현하는 클래스
class Contents : public std::enable_shared_from_this<Contents>
{
public:
    explicit Contents(std::shared_ptr<DbWorkerPool> db);
    void Dispatch(JobType type);
private:
    void handlePrintMessage();
    void handleLoadGold();
    static DetachedTask loadGoldAsync(std::shared_ptr<Contents> spContents, std::shared_ptr<IResumeExecutor> executor);

    std::shared_ptr<DbWorkerPool> m_db;
};
