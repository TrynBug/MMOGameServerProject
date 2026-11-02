#include "DB.h"
#include "Contents.h"

Contents::Contents(std::shared_ptr<DbWorkerPool> db) : m_db(std::move(db)) {}

void Contents::Dispatch(JobType type)
{
    switch (type)
    {
    case JobType::PrintMessage:
        handlePrintMessage();
        break;
    case JobType::LoadGold:
        handleLoadGold();
        break;
    }
}

void Contents::handlePrintMessage()
{
    Trace("PrintMessage job: hello");
}

// IOCP worker 스레드에서 호출되는 job 처리 함수이다. 코루틴을 사용하여 DB 요청을 비동기로 처리한다.
void Contents::handleLoadGold()
{
	std::shared_ptr<Contents> contents = shared_from_this();

    // ResumeExecutor는 코루틴이 재개될 때 어느 스레드에서 재개되는지를 결정하는 컴포넌트이다.
    std::shared_ptr<ResumeExecutor> resumeExecutor = IocpExecutor::Instance()->GetResumeExecutor();

    /* [#코루틴 01 | IOCP worker 스레드] loadGoldAsync 코루틴 함수를 호출한다.
    loadGoldAsync는 코루틴 함수인데, 함수 안에서 co_await을 사용하기 때문이다.
    코루틴 함수를 호출하면 함수가 실제로 호출되기 전에 컴파일러가 아래 준비 절차를 수행하는 코드를 생성한다:
        1. 함수 인자를 평가한다. contents, resumeExecutor 인자를 함수에 전달한다.
        2. 코루틴 프레임을 생성한다. 코루틴 프레임은 코루틴이 시작되고 끝날 때까지 필요한 정보들이 저장되어 있다.
           코루틴 프레임에는 코루틴이 중단된 지점, 재개될 지점, 함수 인자, 현재 함수의 지역변수, awaiter 객체, promise_type 객체 등이 있다.
        3. DetachedTask::promise_type 객체를 생성하여 코루틴 프레임의 promise 멤버에 저장한다.
           DetachedTask 는 loadGoldAsync 함수의 반환 타입인데, 이 타입은 반드시 promise_type 이라는 이름의 구조체 정의를 가지고 있어야 한다.
           컴파일러는 정확히 promise_type 이름의 구조체를 찾아서 생성한다.
        4. 코루틴 프레임의 promise.get_return_object() 를 호출하여 호출자에게 반환할 DetachedTask 객체를 만든다.
        5. promise.initial_suspend() 를 호출하여 함수 본문을 실행할 지 아니면 코루틴을 중단할지를 결정한다.
           지금 promise.initial_suspend() 함수는 std::suspend_never 를 반환하기 때문에, 코루틴을 중단하지 않고 바로 loadGoldAsync 함수 본문을 실행한다. */
    loadGoldAsync(contents, resumeExecutor);

    // [#코루틴 10 | IOCP worker 스레드] loadGoldAsync 함수에서 co_await 으로 코루틴을 suspend(중단)한 뒤, 여기로 제어가 돌아온다.
	// 그래서 handleLoadGold 함수는 여기서 종료된다. 이 함수가 종료되기 때문에 IOCP worker 스레드는 계속해서 다른 job을 처리할 수 있다.
    // loadGoldAsync 코루틴은 DB 작업이 완료될 때까지 suspend 상태로 남아있다. 코루틴 재개에 필요한 정보는 코루틴 프레임에 저장되어 있다.
}

// 코루틴 함수. DB 작업을 요청한다.
DetachedTask Contents::loadGoldAsync(std::shared_ptr<Contents> spContents, std::shared_ptr<ResumeExecutor> executor)
{
    // [#코루틴 04 | IOCP worker 스레드] 함수 본문이 시작된다. 
    // 이 시점에는 코루틴 프레임이 생성된 상태이다.
    
    Trace("LoadGold job: DB request started");
    auto request = std::make_shared<const DbRequest>(DbRequest{ DbOperation::Read, 1, 0 });

    /* [#코루틴 05 | IOCP worker 스레드] Execute 함수를 co_await 한다.
    co_await 키워드로 호출해야 하는 이유는 Execute 함수가 awaitable 객체(DbAwaitable 클래스 객체)를 반환하기 때문이다.
    awaitable은 co_await 할 수 있는 타입을 지칭하는 용어이다.

    co_await 키워드로 함수를 호출하면 컴파일러가 아래 코드를 생성한다:
        1. 함수 인자를 평가하고 함수에 전달한다: request, executor
        2. Execute를 호출하여 DbAwaitable 객체를 생성하고, 코루틴 프레임의 awaiter 멤버에 저장한다.
        3. 코루틴 프레임의 awaiter.await_ready() 함수를 호출한다.
           현재 awaiter.await_ready() 의 반환값은 false 이므로, 4번으로 진행한다.
           만약 반환값이 true 였다면 곧바로 7번으로 진행한다.
        4. 코루틴 프레임에 현재 위치(코루틴 재개 위치)를 기록하고, loadGoldAsync를 suspend(중단) 상태로 만든다.
           그리고 코루틴 프레임에 함수 인자, 지역변수, awaiter를 저장하여 코루틴이 재개되었을 때 접근할 수 있도록 한다.
           이 시점에서 result 변수는 아직 초기화되지 않았다.
        5. awaiter.await_suspend(coroutine_handle) 함수를 호출한다.
           인자로 전달된 coroutine_handle은 현재 코루틴 프레임을 가리키는 핸들이다.
           awaiter.await_suspend(coroutine_handle) 함수의 내용을 확인해보자.
        6. awaiter.await_suspend 함수가 반환되었다.
           그러면 제어는 loadGoldAsync 함수를 호출했던 지점으로 돌아간다.
           즉, Contents::handleLoadGold 함수에서 loadGoldAsync 함수 호출 다음 코드로 돌아간다. */
    auto result = co_await spContents->m_db->Execute(request, executor);
    /*  [#코루틴 18 | IOCP worker 스레드] 누군가 coroutine_handle.resume() 을 호출하여 코루틴이 재개되었다.
        7. awaiter.await_resume() 함수가 호출되었고, 반환값으로 result 변수가 초기화 되었다.
        8. 코루틴 프레임 awaiter에 저장되어있던 DbAwaitable 객체를 파괴한다. */


    // [#코루틴 19 | IOCP worker 스레드] DB 결과를 처리한다.
    if (!result->success)
        Trace("LoadGold job: DB error: " + result->errorMessage);
    else if (result->rows.empty())
        Trace("LoadGold job: player not found");
    else
        Trace("LoadGold job: gold = " + result->rows[0][0]);

    /* [#코루틴 20 | IOCP worker 스레드] 코루틴 함수가 종료된다.
    코루틴 함수가 종료될 때는 컴파일러가 추가한 아래 코드가 실행된다:
        1. promise.return_void() 함수를 호출한다. 
           현재 return_void() 함수 반환타입은 void 이기 때문에 이 코루틴 함수가 리턴할 것은 없다.
        2. 코루틴 프레임에 저장되어 있던 지역변수들을 정리한다.
        3. promise.final_suspend() 함수를 호출한다.
           현재 final_suspend() 함수 반환타입은 std::suspend_never 이기 때문에 코루틴이 suspend되지 않고 함수가 종료된다.
        4. 코루틴 프레임을 제거한다. */
}
