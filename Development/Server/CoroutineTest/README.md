# IOCP와 DB worker 코루틴 예제

IOCP worker 2개와 DB worker 2개를 사용하는 C++20 콘솔 프로그램이다. main은 Contents 객체를 만들고 1초마다 PostJob(type, contents)를 호출한다. IOCP가 내부 job을 생성하고 worker에서 job의 Contents::Dispatch를 호출한다. 컨텐츠가 type별 처리 함수를 실행하고 DB job 처리 함수만 코루틴을 호출한다.

## 준비 및 실행

1. MySQL에서 `Setup.sql`을 실행하여 `CoroutineExample` 스키마와 프로시저를 생성한다.
2. 예제 계정에 `CoroutineExample.*`의 EXECUTE 권한을 부여한다. 프로시저는 설치 계정의 권한으로 테이블에 접근한다.
3. `main.cpp`의 연결 설정(host/user/password/port)을 수정한다.
4. Visual Studio에서 CoroutineTest를 시작 프로젝트로 선택하고 `Debug|x64`로 빌드한다.
5. `bin/x64/Debug/CoroutineTest.exe`를 실행한다. 프로그램은 계속 실행되며 콘솔의 Ctrl+C 또는 디버거 중지로 종료한다.

저장소의 mysql/include와 mysql/lib/libmysql.lib를 사용한다. 빌드 시 OUTPUT/Debug 또는 Release의 MySQL/OpenSSL DLL을 실행 폴더에 복사한다. 라이브러리는 x64 전용이다.

## job 종류

| JobType | 호출 함수 | 처리 |
| --- | --- | --- |
| PrintMessage | Contents::handlePrintMessage | 메시지 출력 |
| LoadGold | Contents::handleLoadGold → loadGoldAsync | 코루틴으로 ReadGold(1)을 호출하고 gold 또는 오류 출력 |

main이 두 종류 중 하나를 무작위로 선택한다. 같은 type이 연속으로 선택될 수 있다. IOCP의 PostJob이 job을 보관하고 worker에서 job->contents->Dispatch(job->type)을 호출한다. 현재 job은 DB를 조회만 하며 데이터를 변경하지 않는다.

IocpJob은 IocpExecutor의 private 타입이다. PostJob이 unique_ptr로 job을 생성하고 completion key로 주소를 전달한다. 등록 실패 시 즉시 해제하며, 성공 시 큐로 소유권을 넘긴다. run이 unique_ptr로 회수하여 일반 job이면 컨텐츠의 Dispatch를 직접 호출하고 처리 후 해제한다. DB 완료 후 재개 함수도 내부 ResumeCoroutine job으로 전달하여 별도로 실행한다. 이 type은 main의 무작위 선택 범위에 포함되지 않는다.

## 실행 순서

`[#시작 01~04]`: main이 DB worker 수만큼 연결을 미리 연다. 하나라도 실패하면 연결들을 정리하고 exit code 1로 종료한다. 모두 성공한 뒤 DB worker와 IOCP worker를 시작한다.

`[#job 01~04]`: main이 PostJob(type, contents) 호출 → IOCP가 job 생성/등록 → worker가 컨텐츠의 Dispatch 호출 → 컨텐츠가 type별 처리 함수 호출.

Contents::handleLoadGold에서 시작하는 코루틴은 `[#코루틴 01~19]`를 따라 읽는다.

- 01~04: 프레임과 promise를 준비하고 본문을 실행한다.
- 05~08: Execute가 awaiter를 반환한다. await_ready(false) 후 중단하고 await_suspend에서 DB Job을 등록한다.
- 09: IOCP 작업으로 돌아간다. worker는 다음 job 또는 재개 작업을 처리한다.
- 10~12: DB worker가 프로시저를 동기로 호출하고 결과를 채운 뒤 IOCP에 재개 작업을 등록한다.
- 13~15: IOCP worker 중 하나가 handle.resume()를 실행하고 await_resume의 결과로 후속 처리를 수행한다.
- 16~19: job을 끝내고 코루틴 프레임을 해제한다. worker는 다음 IOCP 작업을 기다린다.

09와 10~12는 병행된다. DB가 빠르면 await_suspend 반환 전 다른 IOCP worker가 재개할 수도 있다. 시작 worker와 재개 worker는 달라도 된다. main은 job 완료를 기다리지 않고 계속 생산한다.

## 구성과 수명

IocpExecutor::Instance()는 같은 싱글톤 객체를 반환한다. main은 Start로 worker를 시작한다. IocpExecutor는 ResumeExecutor를 상속하지 않고 재개 컴포넌트를 소유한다. Contents::handleLoadGold가 Instance()->GetResumeExecutor()를 직접 호출하여 코루틴에 전달한다. main, job, Dispatch는 executor를 전달하지 않는다. GetResumeExecutor는 aliasing shared_ptr로 컴포넌트와 IOCP의 수명을 연결한다.

- main.cpp: DB 및 IOCP 시작, Contents 생성, 무작위 job 생산 루프.
- Contents.h/cpp: JobType, 컨텐츠 Dispatch, job 처리 함수와 DB 코루틴.
- Example.h/cpp: IOCP executor와 내부 IocpJob 소유권 관리, 재개 컴포넌트와 DetachedTask.
- DB.h/cpp: DB 설정·요청·결과 타입, DbAwaitable, DB 큐와 연결 관리. 번호 05~12 및 14의 DB 대기·처리 흐름을 따라 읽는다.
- Setup.sql: 예제 테이블과 프로시저. 현재 실행 흐름에서는 ReadGold만 호출한다.

IOCP job 자체는 IOCP만 소유하고 job 안의 컨텐츠는 shared_ptr로 유지한다. DB 요청은 shared_ptr<const DbRequest>, 결과는 shared_ptr<DbResult>로 전달한다. Execute는 일반 함수이며 await_suspend에서 실제 요청을 등록한다. loadGoldAsync 호출마다 독립된 프레임을 만들며 중첩 코루틴은 없다. 프레임은 spContents를 보관하여 job이 소멸한 뒤에도 컨텐츠와 그 컨텐츠가 소유한 DB pool을 유지한다.

Contents에 전용 스레드는 없다. 여러 IOCP worker가 같은 객체의 Dispatch와 후속 처리를 실행할 수 있다. 현재 컨텐츠는 변경 가능한 게임 상태를 보관하지 않으며 shared_ptr는 메모리 수명만 보호한다.

DbWorkerPool은 연결들을 소유하고 connection[index]를 worker[index]에 고정 배정한다. worker는 이미 연결된 핸들로 작업하며 mysql_thread_init으로 스레드 로컬 자원만 준비한다. 초기 연결 실패는 프로그램을 종료하고, 실행 중 DB 오류는 해당 job의 결과로 전달하여 로그에 남긴다.

main의 정상 실행은 무한루프이며 완료 콜백이나 자동 종료 절차는 없다. 콘솔/디버거 종료는 진행 중 작업의 완료를 기다리는 graceful shutdown이 아니다. 재연결, 재시도, 취소, 컨텐츠 스레드, AsyncPin은 구현하지 않는다.
