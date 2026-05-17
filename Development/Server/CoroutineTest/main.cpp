// CoroutineTest - DBConnector 코루틴 사용 예시 모음
//
// 빌드 요구사항:
//   - C++20 (/std:c++20)
//   - DBConnector.lib 링크
//   - DBConnector 프로젝트 include 경로 추가
//
// 예시 목록:
//   예시 1. 가장 기본적인 co_await 사용법
//   예시 2. 콜백 중첩 vs 코루틴 비교 (동일한 로직 두 가지 방식으로)
//   예시 3. 여러 DB 요청을 순차적으로 처리
//   예시 4. 코루틴에서 오류 처리
//   예시 5. 코루틴을 리턴값(DBTask<T>)으로 조합하기
//   예시 6. executor를 통한 resume 스레드 제어

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "DBConnectorLib.h"

// ─────────────────────────────────────────────────────────────────────────────
// SimpleThreadPool
//
// IResumeExecutor 구현체. 코루틴 resume을 실행할 스레드 풀.
// 실제 서버에서는 이 역할을 IOCP Worker 스레드 풀이 담당한다.
// ─────────────────────────────────────────────────────────────────────────────
class SimpleThreadPool : public db::IResumeExecutor
{
public:
    explicit SimpleThreadPool(int numThreads)
    {
        for (int i = 0; i < numThreads; ++i)
        {
            m_threads.emplace_back([this]()
            {
                while (true)
                {
                    std::function<void()> fn;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this] { return !m_queue.empty() || m_stop; });
                        if (m_stop && m_queue.empty())
                            return;
                        fn = std::move(m_queue.front());
                        m_queue.erase(m_queue.begin());
                    }
                    fn();
                }
            });
        }
    }

    ~SimpleThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_threads)
            t.join();
    }

    void Post(std::function<void()> fn) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(std::move(fn));
        }
        m_cv.notify_one();
    }

    void WaitAllDone(int expectedCount)
    {
        std::unique_lock<std::mutex> lock(m_doneMutex);
        m_doneCv.wait(lock, [this, expectedCount] { return m_doneCount >= expectedCount; });
    }

    void NotifyDone()
    {
        {
            std::lock_guard<std::mutex> lock(m_doneMutex);
            ++m_doneCount;
        }
        m_doneCv.notify_all();
    }

private:
    std::vector<std::thread>           m_threads;
    std::vector<std::function<void()>> m_queue;
    std::mutex                         m_mutex;
    std::condition_variable            m_cv;
    bool                               m_stop = false;

    std::mutex                         m_doneMutex;
    std::condition_variable            m_doneCv;
    int                                m_doneCount = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// 테스트용 DB 초기화
// ─────────────────────────────────────────────────────────────────────────────
void initTestDB(db::DBConnection& conn)
{
    conn.Execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id    INTEGER PRIMARY KEY AUTOINCREMENT,
            name  TEXT    NOT NULL,
            score INTEGER NOT NULL DEFAULT 0
        )
    )");

    conn.Execute(R"(
        CREATE TABLE IF NOT EXISTS items (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            name    TEXT    NOT NULL
        )
    )");

    conn.Execute("DELETE FROM users");
    conn.Execute("DELETE FROM items");

    conn.Execute("INSERT INTO users (name, score) VALUES ('Alice', 100)");
    conn.Execute("INSERT INTO users (name, score) VALUES ('Bob',   200)");
    conn.Execute("INSERT INTO users (name, score) VALUES ('Carol', 150)");

    conn.Execute("INSERT INTO items (user_id, name) VALUES (1, 'Sword')");
    conn.Execute("INSERT INTO items (user_id, name) VALUES (1, 'Shield')");
    conn.Execute("INSERT INTO items (user_id, name) VALUES (2, 'Bow')");

    std::cout << "[DB] 초기화 완료\n\n";
}


// =============================================================================
//
// 예시 1. 가장 기본적인 co_await 사용법
//
// 핵심 개념:
//   - 함수 반환타입을 db::DBTask<void>로 선언하면 코루틴이 됨
//   - co_await dbQueue.ExecuteAsync(...) 로 DB 결과를 기다림
//   - DB worker가 처리 완료되면 코루틴이 재개됨
//   - 코드 흐름이 위에서 아래로 직선적 (콜백 중첩 없음)
//
// =============================================================================
db::DBTask<void> example1_basic(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시1] SELECT 시작\n";

    db::DBResult result = co_await dbQueue.ExecuteAsync(
        "SELECT id, name, score FROM users ORDER BY score DESC",
        {},
        &pool   // resume을 pool 스레드에서 실행
    );

    std::cout << "[예시1] 결과 (" << result.RowCount() << "건):\n";
    for (int i = 0; i < result.RowCount(); ++i)
    {
        std::cout << "  " << result.GetString(i, "name")
                  << " - score: " << result.GetInt64(i, "score") << "\n";
    }

    pool.NotifyDone();
}


// =============================================================================
//
// 예시 2. 콜백 중첩 방식 vs 코루틴 방식 비교
//
// 같은 로직 (유저 조회 → 점수 업데이트 → 결과 확인) 을 두 가지로 구현.
//
// =============================================================================

// 2-A. 콜백 중첩 방식 (기존 방식)
void example2_callback(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시2-콜백] 시작\n";

    dbQueue.Execute("SELECT id FROM users WHERE name = 'Alice'", {},
        [&dbQueue, &pool](db::DBResult r1)
        {
            if (r1.IsEmpty()) { pool.NotifyDone(); return; }

            int64_t userId = r1.GetInt64(0, "id");

            dbQueue.Execute("UPDATE users SET score = score + 50 WHERE id = ?", { userId },
                [&dbQueue, &pool, userId](db::DBResult r2)
                {
                    dbQueue.Execute("SELECT name, score FROM users WHERE id = ?", { userId },
                        [&pool](db::DBResult r3)
                        {
                            std::cout << "[예시2-콜백] 업데이트 후: "
                                      << r3.GetString(0, "name")
                                      << " score=" << r3.GetInt64(0, "score") << "\n";
                            pool.NotifyDone();
                        });
                });
        });
}

// 2-B. 코루틴 방식 (동일 로직)
db::DBTask<void> example2_coroutine(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시2-코루틴] 시작\n";

    db::DBResult r1 = co_await dbQueue.ExecuteAsync(
        "SELECT id FROM users WHERE name = 'Bob'", {}, &pool);
    if (r1.IsEmpty()) { pool.NotifyDone(); co_return; }

    int64_t userId = r1.GetInt64(0, "id");

    co_await dbQueue.ExecuteAsync(
        "UPDATE users SET score = score + 50 WHERE id = ?", { userId }, &pool);

    db::DBResult r3 = co_await dbQueue.ExecuteAsync(
        "SELECT name, score FROM users WHERE id = ?", { userId }, &pool);

    std::cout << "[예시2-코루틴] 업데이트 후: "
              << r3.GetString(0, "name")
              << " score=" << r3.GetInt64(0, "score") << "\n";

    pool.NotifyDone();
}


// =============================================================================
//
// 예시 3. 여러 DB 요청을 순차적으로 처리
//
// 핵심 개념:
//   - co_await를 여러 번 사용해 순차적으로 처리
//   - 각 단계에서 이전 결과를 다음 쿼리에 활용
//   - 로그인 처리 흐름과 유사한 패턴
//
// =============================================================================
db::DBTask<void> example3_sequential(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시3] 순차 처리 시작\n";

    // 1단계: 유저 조회
    db::DBResult users = co_await dbQueue.ExecuteAsync(
        "SELECT id, name FROM users", {}, &pool);
    std::cout << "[예시3] 1단계 - 유저 " << users.RowCount() << "명 조회\n";

    // 2단계: 첫 번째 유저의 아이템 조회
    int64_t firstUserId = users.GetInt64(0, "id");
    std::string firstName = users.GetString(0, "name");

    db::DBResult items = co_await dbQueue.ExecuteAsync(
        "SELECT name FROM items WHERE user_id = ?", { firstUserId }, &pool);
    std::cout << "[예시3] 2단계 - " << firstName << "의 아이템 " << items.RowCount() << "개\n";
    for (int i = 0; i < items.RowCount(); ++i)
        std::cout << "   - " << items.GetString(i, "name") << "\n";

    // 3단계: 새 아이템 추가
    co_await dbQueue.ExecuteAsync(
        "INSERT INTO items (user_id, name) VALUES (?, 'Magic Staff')", { firstUserId }, &pool);
    std::cout << "[예시3] 3단계 - " << firstName << "에게 'Magic Staff' 추가\n";

    // 4단계: 아이템 다시 조회
    db::DBResult itemsAfter = co_await dbQueue.ExecuteAsync(
        "SELECT name FROM items WHERE user_id = ?", { firstUserId }, &pool);
    std::cout << "[예시3] 4단계 - 추가 후 아이템 " << itemsAfter.RowCount() << "개\n";
    for (int i = 0; i < itemsAfter.RowCount(); ++i)
        std::cout << "   - " << itemsAfter.GetString(i, "name") << "\n";

    pool.NotifyDone();
}


// =============================================================================
//
// 예시 4. 코루틴에서 오류 처리
//
// 핵심 개념:
//   - 코루틴 안에서 일반 if문으로 result.success 확인
//   - 실패 시 중간에 co_return으로 빠져나오거나 복구 로직 실행
//
// =============================================================================
db::DBTask<void> example4_errorHandling(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시4] 오류 처리 시작\n";

    // 잘못된 쿼리로 실패 케이스 유발
    db::DBResult badResult = co_await dbQueue.ExecuteAsync(
        "SELECT * FROM nonexistent_table", {}, &pool);

    if (!badResult.success)
    {
        std::cout << "[예시4] DB 실패 감지: " << badResult.errorMsg << "\n";
        std::cout << "[예시4] 복구 로직 실행 (정상 쿼리로 폴백)\n";
    }

    // 폴백으로 정상 쿼리 수행
    db::DBResult fallback = co_await dbQueue.ExecuteAsync(
        "SELECT COUNT(*) as cnt FROM users", {}, &pool);
    std::cout << "[예시4] 폴백 성공 - 유저 수: " << fallback.GetInt64(0, "cnt") << "\n";

    pool.NotifyDone();
}


// =============================================================================
//
// 예시 5. DBTask<T>로 코루틴 조합하기
//
// 핵심 개념:
//   - 코루틴이 값을 리턴할 수 있음 (DBTask<int64_t> 등)
//   - 코루틴을 함수처럼 co_await로 호출해서 조합 가능
//   - 복잡한 로직을 작은 코루틴으로 분리하는 패턴
//
// =============================================================================

// 유저 ID를 받아서 점수를 리턴하는 코루틴
db::DBTask<int64_t> getScore(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool, int64_t userId)
{
    db::DBResult r = co_await dbQueue.ExecuteAsync(
        "SELECT score FROM users WHERE id = ?", { userId }, &pool);

    if (r.IsEmpty())
        co_return -1;

    co_return r.GetInt64(0, "score");
}

// getScore 코루틴을 co_await로 조합하는 코루틴
db::DBTask<void> example5_composition(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시5] 코루틴 조합 시작\n";

    // 유저 목록 조회
    db::DBResult users = co_await dbQueue.ExecuteAsync(
        "SELECT id, name FROM users", {}, &pool);

    // 각 유저의 점수를 코루틴으로 조회 (co_await로 조합)
    for (int i = 0; i < users.RowCount(); ++i)
    {
        int64_t userId = users.GetInt64(i, "id");
        std::string name = users.GetString(i, "name");

        // 다른 코루틴을 co_await로 호출
        int64_t score = co_await getScore(dbQueue, pool, userId);

        std::cout << "[예시5] " << name << " - score: " << score << "\n";
    }

    pool.NotifyDone();
}


// =============================================================================
//
// 예시 6. executor를 통한 resume 스레드 제어
//
// 핵심 개념:
//   - ExecuteAsync에 executor를 전달하면 코루틴 resume이 해당 스레드 풀에서 실행됨
//   - nullptr이면 DB worker 스레드에서 직접 resume
//   - 실제 서버에서는 executor로 IOCP Worker 스레드 풀을 연결하면 됨
//
// =============================================================================
db::DBTask<void> example6_executor(db::AsyncDBQueue& dbQueue, SimpleThreadPool& pool)
{
    std::cout << "[예시6] executor 제어 시작\n";

    // executor 있음: pool 스레드에서 resume
    {
        auto tid_before = std::this_thread::get_id();
        co_await dbQueue.ExecuteAsync(
            "SELECT name FROM users LIMIT 1", {}, &pool);   // &pool = executor
        auto tid_after = std::this_thread::get_id();

        std::cout << "[예시6] executor 있음\n";
        std::cout << "   co_await 전 thread id: " << tid_before << "\n";
        std::cout << "   co_await 후 thread id: " << tid_after  << "\n";
        std::cout << "   (pool 스레드에서 resume됨)\n";
    }

    // executor 없음: DB worker 스레드에서 직접 resume
    {
        auto tid_before = std::this_thread::get_id();
        co_await dbQueue.ExecuteAsync(
            "SELECT name FROM users LIMIT 1", {}, nullptr);  // nullptr = executor 없음
        auto tid_after = std::this_thread::get_id();

        std::cout << "[예시6] executor 없음\n";
        std::cout << "   co_await 전 thread id: " << tid_before << "\n";
        std::cout << "   co_await 후 thread id: " << tid_after  << "\n";
        std::cout << "   (DB worker 스레드에서 직접 resume됨)\n";
    }

    pool.NotifyDone();
}


// =============================================================================
// main
// =============================================================================
int main()
{
    // DB 초기화
    {
        db::DBConnection conn;
        conn.Open("TestDB.db");
        initTestDB(conn);
    }

    // AsyncDBQueue: DB worker 스레드 1개
    db::AsyncDBQueue dbQueue;
    if (!dbQueue.Open("TestDB.db", 1))
    {
        std::cout << "DB 열기 실패\n";
        return -1;
    }

    // Resume 스레드 풀: 스레드 2개
    SimpleThreadPool pool(2);

    // DBTask 객체를 변수에 보관해야 한다.
    // 리턴값을 버리면 소멸자가 코루틴 프레임을 즉시 해제하고,
    // 이후 DB worker가 resume을 시도할 때 크래시가 발생한다.

    std::cout << "==============================\n";
    std::cout << " 예시 1: 기본 co_await\n";
    std::cout << "==============================\n";
    auto task1 = example1_basic(dbQueue, pool);
    pool.WaitAllDone(1);

    std::cout << "\n==============================\n";
    std::cout << " 예시 2: 콜백 vs 코루틴 비교\n";
    std::cout << "==============================\n";
    example2_callback(dbQueue, pool);
    auto task2 = example2_coroutine(dbQueue, pool);
    pool.WaitAllDone(3);  // 1(예시1) + 1(콜백) + 1(코루틴)

    std::cout << "\n==============================\n";
    std::cout << " 예시 3: 순차 처리\n";
    std::cout << "==============================\n";
    auto task3 = example3_sequential(dbQueue, pool);
    pool.WaitAllDone(4);

    std::cout << "\n==============================\n";
    std::cout << " 예시 4: 오류 처리\n";
    std::cout << "==============================\n";
    auto task4 = example4_errorHandling(dbQueue, pool);
    pool.WaitAllDone(5);

    std::cout << "\n==============================\n";
    std::cout << " 예시 5: 코루틴 조합\n";
    std::cout << "==============================\n";
    auto task5 = example5_composition(dbQueue, pool);
    pool.WaitAllDone(6);

    std::cout << "\n==============================\n";
    std::cout << " 예시 6: executor 스레드 제어\n";
    std::cout << "==============================\n";
    auto task6 = example6_executor(dbQueue, pool);
    pool.WaitAllDone(7);

    std::cout << "\n모든 예시 완료\n";

    dbQueue.Close();
    return 0;
}
