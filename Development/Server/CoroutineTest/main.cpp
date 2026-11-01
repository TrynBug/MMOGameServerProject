#include "Example.h"
#include <mysql.h>
#include <iostream>

namespace
{
    // 실행 흐름: main에서 초기 DB 조회 → IOCP/컨텐츠에서 DB 요청 → 완료 통지 수집 → 스레드 종료.
    // 각 코루틴은 DB를 기다리는 동안 실행 위치와 필요한 데이터를 프레임에 유지한다.
    // [프레임 상태]는 살아 있는 데이터를 설명한다. 실제 메모리 배치는 컴파일러가 결정한다.
    // 프레임의 shared_ptr는 별도로 할당된 객체를 가리키며, 해당 객체 자체를 프레임 안에 넣지는 않는다.

    std::shared_ptr<const DbRequest> MakeRequest(DbOperation operation, int playerId, int gold = 0)
    {
        // 처음부터 const 객체를 생성하여 DB 대기 중 호출부가 입력을 변경할 수 없게 한다.
        return std::make_shared<const DbRequest>(DbRequest{ operation, playerId, gold });
    }

    // 코루틴 함수. DB 작업을 요청한다.
    // 이 함수가 코루틴 함수인 이유는 co_await을 사용하기 때문이다.
    DetachedTask Initialize(DbWorkerPool& db, std::shared_ptr<ResumeExecutor> executor, std::function<void(bool)> onFinished)
    {
        // [#코루틴 04 | main 스레드] Initialize 함수 본문이 시작된다.
        // 이 시점에는 코루틴 프레임이 생성된 상태이다. 

        // 현재 스레드 기록
        const auto launchThread = std::this_thread::get_id();
        Trace("main: initialization started");

        // DB request 생성. 수행할 DB 작업 내용을 담고있다.
        std::shared_ptr<const DbRequest> request = MakeRequest(DbOperation::Read, 1);

        /* [#코루틴 04 - 1 | main 스레드] Execute 함수를 co_await 한다.
        co_await 키워드로 호출해야 하는 이유는 Execute 함수가 awaitable 객체(DbAwaitable 클래스 객체)를 반환하기 때문이다.
        awaitable은 co_await 할 수 있는 타입을 지칭하는 용어이다.

        co_await 키워드로 함수를 호출하면 컴파일러가 아래 코드를 생성한다:
            1. 함수 인자를 평가하고 함수에 전달한다: request, executor
            2. db.Execute를 호출하여 DbAwaitable 객체를 생성하고, 코루틴 프레임의 awaiter 멤버에 저장한다.
            3. 코루틴 프레임의 awaiter.await_ready() 함수를 호출한다.
               현재 awaiter.await_ready() 의 반환값은 false 이므로, 4번으로 진행한다.
               만약 반환값이 true 였다면 곧바로 7번으로 진행한다.
            4. 코루틴 프레임에 현재 위치(코루틴 재개 위치)를 기록하고, Initialize를 suspend(중단) 상태로 만든다.
               그리고 코루틴 프레임에 함수 인자, 지역변수, awaiter를 저장하여 코루틴이 재개되었을 때 접근할 수 있도록 한다.
               이 시점에서 result 변수는 아직 초기화되지 않았다.
            5. awaiter.await_suspend(coroutine_handle) 함수를 호출한다.
			   인자로 전달된 coroutine_handle은 현재 코루틴 프레임을 가리키는 핸들이다.
               awaiter.await_suspend(coroutine_handle) 함수의 내용을 확인해보자.
            6. awaiter.await_suspend 함수가 반환되었다. 
               그러면 제어는 Initialize 함수를 호출했던 지점으로 돌아간다.
               즉, main 함수에서 Initialize 함수 호출 다음 코드로 돌아간다.

        [#코루틴 13 - 1 | main 스레드] 누군가 coroutine_handle.resume() 을 호출하여 코루틴이 재개되었다. 
            7. awaiter.await_resume() 함수가 호출되고, 반환값으로 result 변수를 초기화한다.
               이 문장이 끝나면 임시 awaiter를 정리하고 아래 결과 처리 코드로 진행한다. */
        auto result = co_await db.Execute(request, executor);

        // [#코루틴 15 | main] await_resume의 반환값으로 result를 초기화하고 조회 결과를 처리한다.
        Check(launchThread == std::this_thread::get_id(), "main thread mismatch");
        if (!result->success)
            Trace("initial read failed: " + result->errorMessage);
        Trace("main: initialization resumed");
        onFinished(result->success); // 완료 콜백으로 이동한다(16).
        // 본문 끝에 도달하면 return_void(17), 지역변수 정리, final_suspend(18) 순으로 진행한다.
    }

    // IOCP에서 저장, 조회, 오류 반환, 결과 무시의 네 가지 DB 호출을 순서대로 실행한다.
    // 마지막 조회를 제외한 처리 결과를 모아 onFinished로 성공 여부를 전달한다.
    DetachedTask RunIocp(DbWorkerPool& db, std::shared_ptr<ResumeExecutor> executor, std::function<void(bool)> onFinished)
    {
        // 각 co_await는 같은 프레임의 실행을 중단한다. 재개 작업은 IOCP worker 중 하나가 실행한다.
        Trace("IOCP: started (resume may use another IOCP worker)");
        // [프레임 상태: 첫 번째 대기]
        // promise/재개 위치 + 인자 { db 참조, executor, onFinished } + Write(1, 100) awaiter.
        // result는 초기화 전이다. 아래 각 대기는 모두 이 하나의 프레임을 재사용한다.
        auto result = co_await db.Execute(MakeRequest(DbOperation::Write, 1, 100), executor);
        bool success = result->success;
        if (!result->success)
            Trace("write failed: " + result->errorMessage);

        // Write 처리가 끝난 뒤 조회를 요청한다. 저장 성공 시 autocommit된 값을 조회할 수 있다.
        // [프레임 상태: 두 번째 대기]
        // 인자 + success + result(이전 Write 결과) + 새 Read(1) awaiter(별도의 새 결과 버퍼).
        // 대입은 await_resume 이후에 일어나므로 대기 중 result는 여전히 이전 결과를 가리킨다.
        result = co_await db.Execute(MakeRequest(DbOperation::Read, 1), executor);
        // [프레임 상태: 두 번째 재개 후] result가 Read 결과로 교체되고 이번 awaiter는 소멸했다.
        success = success && result->success;
        if (!result->success)
            Trace("read failed: " + result->errorMessage);

        // FailExample 프로시저의 SIGNAL 오류를 받는다. 의도한 오류 코드 1644이면 정상 처리로 판단한다.
        // [프레임 상태: 세 번째 대기] success와 이전 Read 결과를 유지하고 awaiter는 Fail 요청/새 결과 버퍼를 보유한다.
        result = co_await db.Execute(MakeRequest(DbOperation::Fail, 1), executor);
        Trace("intentional procedure error: " + result->errorMessage);
        success = success && !result->success && result->errorCode == 1644;

        // 조회 완료까지 기다린 뒤 반환 결과를 버린다. 이 요청의 성공 여부는 success에 반영하지 않는다.
        // [프레임 상태: 네 번째 대기]
        // 인자 + success + result(직전 Fail 결과) + Read(1) awaiter가 유지된다. 반환값을 버려도 awaiter는 필요하다.
        // 재개 후 Read 결과를 result에 대입하지 않으므로 기존 result는 바뀌지 않는다.
        co_await db.Execute(MakeRequest(DbOperation::Read, 1), executor);
        Trace("IOCP: success/error/ignored-result requests completed");
        onFinished(success);
    }

    // 플레이어의 gold를 저장하는 동안 컨텐츠/캐릭터를 보호하고, 저장 후 보류된 제거를 처리한다.
    // executor는 이 Contents를 소유한 스레드의 큐다.
    DetachedTask RunContents(DbWorkerPool& db, std::shared_ptr<QueueExecutor> executor,
        std::shared_ptr<Contents> contents, std::function<void(bool)> onFinished, int playerId)
    {
        Trace("Contents: started");
        // [프레임 상태: 본문 진입]
        // promise/재개 위치 + 인자 { db 참조, executor, contents, onFinished, playerId }.
        // 각 RunContents 호출은 독립된 프레임이다. 두 컨텐츠가 프레임이나 pin을 공유하지 않는다.
        bool success = false;
        {
            // pin이 살아 있는 동안 컨텐츠 제거와 캐릭터 이동을 막는다.
            AsyncPin pin(contents);
            // [프레임 상태: 핀 획득 후]
            // 인자 + success + pin { m_contents, m_character의 shared_ptr }.
            // inFlightAsync/pendingAsync 카운터는 프레임 내부 값이 아니라 pin이 가리키는 객체의 멤버다.
            // DB 대기 중 제거/이동 요청이 들어오는 상황을 만든다.
            // 제거 작업을 DB 요청보다 먼저 큐에 넣어 코루틴 재개 전에 실행되도록 한다.
            executor->Post([contents]
            {
                contents->RequestRemove();
                Trace(contents->TryMove() ? "Contents: move allowed" : "Contents: move deferred by AsyncPin");
            });

            // [프레임 상태: DB 대기 중]
            // 인자 + success + 살아 있는 pin + Write(playerId, 200) awaiter. result는 초기화 전이다.
            // 제거 요청 람다는 프레임이 아닌 executor 큐가 보관한다. 이 람다가 실행되어도 pin은 소멸하지 않는다.
            auto result = co_await db.Execute(MakeRequest(DbOperation::Write, playerId, 200), executor);
            // [프레임 상태: DB 재개 후]
            // awaiter는 소멸했고 인자 + success + pin + result가 살아 있다. 따라서 후속 처리 동안에도 객체를 보호한다.
            contents->CheckOwner();
            success = result->success;
            if (!result->success)
                Trace("contents write failed: " + result->errorMessage);
            Trace("Contents: resumed on original thread, pin still held");
        } // 후속 처리까지 마친 뒤 pin 소멸: 카운터 감소 및 보류된 제거 처리.
        // [프레임 상태: 내부 scope 종료]
        // result와 pin은 소멸하고 보류된 제거가 처리되었다. success에는 저장 성공 여부가 남아 있다.
        // 인자 contents가 객체를 소유하므로 논리적으로 제거된 Contents의 메모리는 본문 종료까지 유지된다.
        onFinished(success);
    }

}

int main()
{
    // DB 라이브러리를 초기화 한다.
    if (mysql_library_init(0, nullptr, nullptr) != 0)
        return 1;

    int exitCode = 0;
    
    // DB 연결 정보를 설정한다.
    auto config = std::make_shared<DbConfig>();
    config->host = "127.0.0.1";
    config->user = "coroutine_example";
    config->password = "";
    config->port = 3306;

    // DB worker 스레드를 생성한다.
    // worker 2개를 생성한다. 각각의 worker는 자신만의 DB 연결을 사용한다.
    DbWorkerPool db(config, 2); 

    // main 스레드에서 재개할 코루틴과 완료통지를 저장해둘 Queue를 만든다.
    // DB 작업이 완료되면 DB worker는 이 큐에 코루틴 재개 작업을 넣어주게 된다.
    auto mainExecutor = std::make_shared<QueueExecutor>();

    int finished = 0;

    // 이 함수는 DB worker에서 DB 작업이 완료된 후 호출되는데, mainExecutor의 큐에 코루틴 재개 작업을 넣는 역할을 한다.
    // 여기서 캡처된 변수들은 코루틴이 재개될 때까지 유지되어야 한다.
    auto onFinished = [mainExecutor, &finished, &exitCode](bool success)
    {
        // [#코루틴 16 | main 스레드] 완료 통지를 main 큐에 넣고 Initialize로 돌아간다.
        // 여기서는 finished가 바뀌지 않는다. 현재 재개 작업이 끝난 후 다음 RunOne에서 처리한다.
        mainExecutor->Post([&finished, &exitCode, success]
        {
            // [#코루틴 20 | main] 다음 RunOne이 완료 통지를 실행하여 finished를 1로 만든다.
            ++finished;
            if (!success)
                exitCode = 1;
        });
    };


    /* [#코루틴 01 | main 스레드] Initialize 코루틴 함수를 호출한다.
    Initialize는 코루틴 함수인데, 함수 안에서 co_await을 사용하기 때문이다.
    코루틴 함수를 호출하면 함수가 실제로 호출되기 전에 컴파일러가 아래 준비 절차를 수행하는 코드를 생성한다:
        1. 함수 인자를 평가한다. db, mainExecutor, onFinished 인자를 함수에 전달한다.
        2. 코루틴 프레임을 생성한다. 코루틴 프레임은 코루틴이 시작되고 끝날 때까지 필요한 정보들이 저장되어 있다.
           코루틴 프레임에는 코루틴이 중단된 지점, 재개될 지점, 함수 인자, 현재 함수의 지역변수, awaiter 객체, promise_type 객체 등이 있다.
        3. DetachedTask::promise_type 객체를 생성하여 코루틴 프레임의 promise 멤버에 저장한다. 
           DetachedTask 는 Initialize 함수의 반환 타입인데, DetachedTask 는 반드시 promise_type 이라는 이름의 구조체 정의를 가지고 있어야 한다.
           컴파일러는 정확히 promise_type 이름의 구조체를 찾아서 생성한다.
        4. 코루틴 프레임의 promise.get_return_object() 를 호출하여 호출자에게 반환할 DetachedTask 객체를 만든다.
        5. promise.initial_suspend() 를 호출하여 함수 본문을 실행할 지 아니면 코루틴을 중단할지를 결정한다.
           지금 promise.initial_suspend() 함수는 std::suspend_never 를 반환하기 때문에, 코루틴을 중단하지 않고 바로 Initialize 함수 본문을 실행한다. */
    Initialize(db, mainExecutor, onFinished);

    // [#코루틴 09 | main 스레드] DB 요청 등록 후 Initialize 코루틴이 suspend(중단)되어 여기로 돌아온다.
    // 비동기로 DB 작업이 진행되는 동안 main 스레드는 다른 작업을 해도 되지만, 여기서는 DB 작업이 완료되기를 기다린다.
	// finisied 값이 1이 되면 Initialize 코루틴이 완료되었음을 의미한다. 
    while (finished != 1)
    {
        // main 스레드는 RunOne을 호출하여 mainExecutor의 큐에 있는 작업을 처리한다.
        mainExecutor->RunOne();
    }

    // [#코루틴 21 | main] 완료 통지를 처리한 RunOne이 반환된다. finished == 1이므로 루프를 끝낸다.
    // Initialize 코루틴은 이미 종료되었으며, main은 초기화 성공 여부에 따라 다음 단계로 진행한다.
    // 초기 DB 접근에 성공하면 IOCP와 두 컨텐츠의 저장 절차를 시작한다.
    if (exitCode == 0)
    {
        auto iocp = std::make_shared<IocpExecutor>(2);
        std::vector<std::shared_ptr<QueueExecutor>> contentsExecutors;
        std::vector<std::thread> contentsThreads;
        for (int index = 0; index < 2; ++index)
        {
            auto executor = std::make_shared<QueueExecutor>();
            contentsExecutors.push_back(executor);
            // 컨텐츠마다 큐와 소비 스레드를 하나씩 배정하여 항상 같은 스레드에서 재개되게 한다.
            contentsThreads.emplace_back([executor] { executor->Run(); });
            executor->Post([&db, executor, onFinished, index]
            {
                // 소유 스레드에서 Contents를 생성하고 저장을 시작한다. 대상 플레이어는 각각 2번과 3번이다.
                RunContents(db, executor, std::make_shared<Contents>(), onFinished, index + 2);
            });
        }
        iocp->Post([&db, iocp, onFinished] { RunIocp(db, iocp, onFinished); });

        // 초기화 1개 + 컨텐츠 2개 + IOCP 1개의 완료 통지를 모두 받는다.
        // 이 동안 DB worker와 executor는 계속 실행되어 각 코루틴의 남은 요청을 처리한다.
        while (finished != 4)
            mainExecutor->RunOne();

        // onFinished 통지는 본문 끝 직전에 도착하므로 마지막 resume가 아직 반환 중일 수 있다.
        // DB worker를 먼저 join하고, executor worker도 join한 뒤 관련 객체를 파괴한다.
        db.Stop();
        for (auto& executor : contentsExecutors)
            executor->Stop();
        for (auto& thread : contentsThreads)
            thread.join();
        iocp->Stop(); // main이 명시적으로 join하여 worker에서 executor가 최종 파괴되지 않게 한다.
    }
    else
        db.Stop();

    mainExecutor->Stop();
    Trace(exitCode == 0 ? "All examples completed" : "DB request failed: check errors above");
    

    mysql_library_end();
    return exitCode;
}
