#include "DB.h"
#include "Example.h"
#include "Contents.h"
#include <mysql.h>
#include <chrono>
#include <exception>
#include <random>


int main()
{
    if (mysql_library_init(0, nullptr, nullptr) != 0)
        return 1;

    int exitCode = 0;
    try
    {
        // MySQL 접속정보 설정
        // Setup.sql 파일에 DB 스키마가 있다.
        auto config = std::make_shared<DbConfig>();
        config->host = "127.0.0.1";
        config->user = "coroutine_example";
        config->password = "";
        config->port = 3306;

        // DB worker 스레드 4개 시작
        auto db = std::make_shared<DbWorkerPool>(config, 4);

        // IOCP worker 스레드 2개 시작
        auto iocp = IocpExecutor::Instance();
        iocp->Start(2);

        // job 처리 로직이 있는 Contents 객체 생성
        auto contents = std::make_shared<Contents>(db);

        // 1초마다 IOCP에 job을 등록한다.
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            int r = rand() % static_cast<int>(JobType::MAX);
            const JobType type = static_cast<JobType>(r);

            // IOCP에 job 등록
            iocp->PostJob(type, contents);
        }
    }
    catch (const std::exception& exception)
    {
        Trace(exception.what());
        exitCode = 1;
    }

    mysql_library_end();

    return exitCode;
}
