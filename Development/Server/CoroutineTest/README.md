# DB 코루틴 예제

GameServer/DBConnector 코드에 의존하지 않는 C++20 콘솔 예제다. 실제 MySQL 프로시저를 동기 호출하는 DB worker 2개, 실제 IOCP worker 2개, 컨텐츠 스레드 2개와 main 스레드를 사용한다. 소켓 연결은 만들지 않고 IOCP completion packet으로 작업 전달만 시연한다.

## 준비 및 실행

1. MySQL에서 `Setup.sql`을 실행한다. `CoroutineExample` 전용 스키마에 Players 테이블과 프로시저 3개를 만든다. 재실행하면 이 예제 프로시저만 교체하며 데이터는 삭제하지 않는다.
2. 예제 전용 DB 계정을 만들고 `CoroutineExample.*`에 EXECUTE 권한을 준다. 프로시저는 기본 SQL SECURITY DEFINER로 실행되므로 설치 계정은 테이블 접근 권한도 있어야 한다. 계정 생성과 비밀번호 설정은 환경에 맞게 수행한다.
3. `main.cpp`의 `config`에 하드코딩된 연결 설정을 수정한다. 기본값은 host=`127.0.0.1`, port=`3306`, user=`coroutine_example`, password=빈 문자열이다. 스키마는 `CoroutineExample`을 사용한다.
4. Visual Studio에서 CoroutineTest를 시작 프로젝트로 선택하고 `Debug|x64`로 빌드한다. 저장소의 `mysql/include`, `mysql/lib/libmysql.lib`를 사용하며 `OUTPUT/Debug`의 MySQL/OpenSSL DLL 3개를 실행 폴더에 복사한다. 제공 라이브러리는 x64 전용이다.
5. Visual Studio 또는 다음 PowerShell 명령으로 실행한다. 환경변수 설정은 필요 없다.

```powershell
& .\bin\x64\Debug\CoroutineTest.exe
```

작업 디렉터리는 CoroutineTest 폴더 기준이다. 프로그램은 예제 Players의 1번 gold를 100, 2/3번을 200으로 갱신한다. 여러 프로세스를 동시에 실행하는 테스트는 하지 않는다.

## 읽는 순서

1. `main.cpp`: main 초기화 → IOCP 및 컨텐츠 요청 → 완료 확인 → 종료.
2. `Example.h`의 `DetachedTask`: 유일한 코루틴 반환 타입. 시작/종료 모두 `suspend_never`이며 종료하면 프레임이 자동 파괴된다. 중첩 코루틴/continuation은 없다.
3. `Example.cpp`의 `DbAwaitable`: `await_ready(false)` → `await_suspend`에서 요청 등록 → 재개 시 `await_resume`으로 공통 결과 전달.
4. `DbWorkerPool::run`: worker별 연결로 CALL을 동기 실행하고 모든 결과셋과 마지막 OK까지 소비한다. 그 후 executor에 `handle.resume()`을 등록한다.
5. `QueueExecutor`, `IocpExecutor`: 재개 위치를 결정하는 실행 큐.
6. `AsyncPin`: DB 대기와 후속 처리 동안 컨텐츠/캐릭터 수명 및 논리적 소속을 보호한다.

```text
요청 스레드: 공유 요청 생성 → co_await → 다른 작업 처리
DB worker:  큐에서 요청 획득 → 동기 CALL → 공유 결과 기록 → executor.Post
재개 스레드: 큐에서 resume 실행 → await_resume → 후속 처리 → 프레임 파괴
```

`Execute()`는 코루틴이 아니라 awaitable을 반환하는 일반 함수다. 요청 등록은 `await_suspend()`에서 시작된다. awaiter는 중단 중에도 코루틴 프레임에 유지된다. 요청과 결과는 Job도 shared_ptr로 소유하며, 요청은 `shared_ptr<const DbRequest>`로 전달한다. 비동기 요청에 게임 객체 포인터를 넘기지 않는다. 결과 쓰기와 읽기는 executor 큐의 동기화를 통해 순서가 보장된다.

ResumeExecutor는 필수다. main은 직접 큐를 pump하고, 컨텐츠는 자신만의 큐를 실행하며, IOCP는 공용 포트에서 어느 worker든 재개할 수 있다. 코루틴이 자동으로 원래 스레드로 돌아오는 것이 아니다. main이 큐를 처리하지 않고 완료 신호만 기다리면 교착된다.

## 실행 흐름 확인

- main 초기화 조회: main에서 재개되는지 확인한다. 연결/스키마 오류는 공통 DbResult로 반환하고 exit code 1로 종료한다.
- IOCP: 저장 → 조회 → 의도적인 SQL SIGNAL 오류(1644) → 결과를 무시하는 조회. 전부 같은 Execute/DbResult 경로다.
- 컨텐츠 2개: 각각 자신의 스레드에서만 접근하는지 확인한다. DB 대기 중 제거 요청을 큐에 넣고 제거 보류와 이동 거절을 로그로 보여준다. 핀 해제 시 캐릭터 제거와 컨텐츠 논리적 제거가 완료된다. shared_ptr가 실제 메모리 수명도 보호한다.
- `Completion`과 검증용 `Expect()`는 사용하지 않는다. main의 지역 카운터 `finished`와 `onFinished(bool)` 콜백으로 완료를 기다린다. 콜백은 main 큐에 통지를 넣으므로 카운터와 종료 코드에 atomic이 필요 없다.
- DB 처리가 정상 완료되면 `All examples completed`, exit code 0이다. 의도적인 SIGNAL 오류는 정상 시나리오로 취급하며, 그 밖의 확인한 DB 실패는 로그와 exit code 1로 보고한다. 결과를 무시하는 마지막 조회는 종료 코드에 반영하지 않는다. 이 종료 코드는 제거/이동 상태나 조회값의 자동 검증 결과를 의미하지 않는다. thread ID 로그로 호출/재개 위치를 비교할 수 있다. IOCP의 ID는 같아도 되고 달라도 된다.
- 종료: 신규 최상위 요청 발행 중단 → 기존 코루틴의 후속 요청까지 완료 → DB 큐 종료 및 worker join → 컨텐츠/IOCP 종료 및 join → MySQL 라이브러리 종료. main 완료 알림을 받더라도 executor worker를 명시적으로 join해 마지막 resume 호출이 반환될 때까지 기다린다.

## 예제 범위

프로시저 이름은 코드에 고정하고 정수 파라미터만 사용한다. 임의 SQL/문자열 입력, 재연결/재시도, 취소, 트랜잭션 배치, 샤딩, 중첩 코루틴은 포함하지 않는다. DB worker 연결 실패는 해당 worker의 요청마다 오류 결과로 반환한다. 대기 중 프레임을 강제로 파괴하지 않는다.

SQL 오류는 결과 데이터로 전달한다. 메모리 부족, executor 오류, 잘못된 스레드 접근 같은 C++ 예외/불변식 위반은 예제에서 즉시 실패시킨다. AsyncPin은 컨텐츠 전용이며 카운터는 atomic이 아니다. IOCP 예제는 컨텐츠 객체를 접근하지 않는다.

## 검증 기록

2026-11-01: MSVC `Debug|x64` 빌드 성공. 게임 DB와 분리한 임시 MySQL 8.4.10에서 Setup.sql 설치 후 모든 실행 시나리오 PASS(exit code 0)를 확인했다. 연결 차단 시에도 main 재개 후 실패 결과를 보고하고 exit code 1로 종료하는 것을 확인했다. 임시 DB는 검증 후 종료했다.
