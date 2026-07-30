# DB 종류
- **MySQL (InnoDB) 8.4.10**
  - DB 클라이언트는 **libmysqlclient (Oracle 공식 C 클라이언트)**.
    - C 레벨 API라 현재 `DBConnection`(raw C API 스타일)에 잘 맞고, MySQL 서버 네이티브라 **`caching_sha2_password`(MySQL 8 기본 인증) 무마찰**. blocking + nonblocking API 제공(향후 비동기 여지).
    - 고수준 Connector/C++ 대신 C 레벨을 택한 이유: 풀/코루틴을 직접 관리(`AsyncDBQueue`)하므로 커넥터 내장 풀 불필요 + 기존 raw-C 패턴 적합 + 비동기 여지. (대안 MariaDB Connector/C(LGPL)는 API가 거의 동일해 전환 비용 작음.)

- MySQL 통신 라이브러리: libmysql.dll

# 데이터 저장 형태
- 모든 데이터는 **JSON** 으로 저장. 컬럼 타입은 MySQL `JSON`(바이너리).
- 저장 구조체는 PacketGenerator 의 `Proto/DataStructures` 에 .proto 로 정의(`Generated/DataStructures` 에 .pb.cc/.pb.h).
- 저장 시 `ProtoJsonSerializer::ToJson`, 읽을 때 `FromJson`.
- 현재 테이블의 정확한 컬럼·PK·인덱스·기본값은 `DB스키마.md`를 기준으로 한다. 이 문서는 DB 구성과 동작 정책을 설명한다.

# DB 구성 (Account 1개 + Game N개)
- **AccountDB** : 1개. scale-out 불가(향후 HA 만). 계정정보 + 샤드 디렉터리(`GameDBIndex`).
- **GameDB** : N개. scale-out 가능. 게임플레이 데이터(캐릭터/인벤토리/Gold/스킬/장비 등) 전부.

# 샤딩 설계 (AccountId 기준 디렉터리 샤딩)
- **용어**: `Account`(계정, `account_id`) 1개에 `Character` N개. `User` 는 서버 내에서 1개 클라이언트(세션)를 표현하는 런타임 클래스이고, 영속 식별자는 `account_id` 다.
- 한 계정의 모든 게임데이터는 **1개 GameDB(샤드)에 모인다**(co-location). 샤드 키 = `account_id`.
- 어느 샤드인지는 AccountDB 의 계정 레코드 **`GameDBIndex`** 가 결정(디렉터리 방식 — 해시 %N 아님 → 계정 단위 이동/리밸런싱 가능).
- **게임서버가 입장 시 AccountDB 를 직접 조회**해 `GameDBIndex`(+계정정보)를 읽어 **세션(User 객체)에 캐시**. 이후 그 계정의 모든 저장/읽기는 캐시된 인덱스로 해당 GameDB 커넥션을 선택.
  - LoginServer 가 읽어 전달하는 방식이 아니라 게임서버 직접 조회로 하는 이유: 게임서버도 계정정보가 항상 필요한데, 로그인서버가 읽은 걸 끝까지 들고 다니면 로그인/게이트웨이/게임서버 사이에서 계정 데이터를 계속 주고받아야 하고, 계정정보가 늘 때마다 그 전달 경로를 매번 수정해야 함.
  - `GameDBIndex` 는 **항상 올바르다고 가정**(계정 이주/GameDB 제거는 전체 서버 중지 후 수행 → 라이브 중 변하지 않음).
- **물리 배치**: GameDB 1개 = 1개 샤드. 한 machine 에 여러 GameDB 를 둘 수 있고(예: machine1 = GameDB_00~03, machine2 = GameDB_04,07,08,09 — 중간 05·06 은 제거됨), GameDBIndex 는 논리 번호라 배치는 자유(위치는 `GameDBRegistry` 의 host/port 가 결정).
  - **1 MySQL 인스턴스에 여러 GameDB schema 로 시작**(하나의 큰 buffer pool 공유 → 메모리 효율 + 운영 단순). machine 1대에 샤드 수만큼 프로세스를 띄울 필요 없음. 강한 격리(noisy-neighbor 방지)/샤드별 독립 튜닝·페일오버가 필요해지면 그때 **별도 인스턴스로 분리**. (이주는 어차피 전체 서버 중지 + 데이터 이동이라 인스턴스 분리의 "통째 이동" 이점은 작음.)
  - 팁: **샤드 수(N) > machine 수** 로 넉넉히 만들면, machine 추가 시 GameDB 를 통째 옮기고 host/port 만 갱신하면 됨(계정 단위 재샤딩 불필요).

# ID 규약
- CharacterId / ItemId 등은 **snowflake 방식** → **시각순 증가** + **모든 GameDB에서 전역 유일**. (`ServerBase/ObjectIdGenerator`)
  - 비트구조: `[0(1)] [Timestamp(43, Unix ms 하위)] [ServerID(10)] [Sequence(10, 같은 ms 내 최대 1023)]`.
  - 시각순(timestamp 상위 비트) → InnoDB 클러스터드 PK 에 append → insert 빠름.
  - 전역 유일(ServerID 비트로 서버 구분) → 샤드 이동/조회 안전.
- **운영 조건**: ID 를 생성하는 모든 서버는 **유일한 serverId(1~1023)** 를 가져야 함(중복 시 ID 충돌). serverId 할당의 전역 유일성을 보장하는 절차 필요. (10bit = ID 생성 서버 최대 1023대 상한)

# PK / 인덱스 규약
- PK 는 **엔티티 자기 ID 단일**(예: `character_id`, `item_id`). InnoDB 에서 좁고 단조라 insert 및 2차 인덱스 크기에 유리.
- **샤드 키(`account_id` = 계정 ID)는 2차 인덱스**로 둔다 → 마이그레이션/계정삭제/계정단위 조회(`WHERE account_id=?`)용.
  - InnoDB 2차 인덱스는 PK 를 포함하므로 `INDEX(account_id)` 는 물리적으로 `(account_id, character_id)` 가 되어 계정 스캔에 적합.
- 라우팅은 세션 캐시 `GameDBIndex` 가 하므로 **PK 에 account_id 를 넣을 필요는 없음**(c/d 형태 불필요).

# 트랜잭션
- 한 계정의 데이터는 같은 샤드(1개 GameDB)에 있으므로, 여러 테이블(인벤토리/Gold 등) 동시 변경도 **단일 DB ACID 트랜잭션**으로 처리(분산 트랜잭션 불필요).
- 규칙: 트랜잭션은 **한 커넥션**에서 `BEGIN … COMMIT` 로 묶는다(여러 풀 커넥션에 흩뿌리면 안 됨). → `DBConnection`/`AsyncDBQueue` 에 "한 커넥션에서 여러 쿼리를 묶어 실행"하는 트랜잭션 API 필요.
- 짧게 유지(락 시간↓).
- **데드락 대비 — 예방 + 처리 둘 다**:
  - 예방: **락 순서 일관** — 건드리는 테이블/행의 전역 순서를 정해두고 모든 트랜잭션이 그 순서로 접근(예: 항상 inventory→gold, 같은 테이블 다중 행은 PK 오름차순). 반대 순서 접근이 순환 대기=데드락을 만든다.
  - 처리: **일시적 에러만 선택적 재시도**(횟수 제한 + 백오프). 재시도 대상 = 데드락(1213) / 락대기 타임아웃(1205) / 연결끊김(2006·2013) — 원인이 타이밍이라 재시도하면 대개 성공. 유니크·FK·로직 위반 등 결정적 에러는 **재시도 금지(즉시 실패)**.
  - 락 순서를 일관되게 해도 InnoDB 특성상(갭락/2차인덱스) 데드락을 완전히 없앨 수는 없으므로 위 둘을 함께 적용.
- (향후 ProxySQL) 트랜잭션 동안은 백엔드 커넥션이 고정되어 멀티플렉싱이 잠깐 풀림 → 트랜잭션을 짧게.

# GameDBIndex 할당 (부하 인지 배정)
- AccountDB 에 샤드 레지스트리 테이블(`GameDBRegistry`): `(game_db_index, status, account_count, weight)`.
- 계정 생성 시: `status='active'` 중 **`account_count / weight`(용량 대비 계정 수) 가 최소**인 샤드를 고름.
  - 동시 생성 경쟁 방지: 트랜잭션 + 선택 행 `SELECT … FOR UPDATE` 로 직렬화 후 count 증가(계정 생성은 빈번하지 않아 비용 무시).
- (향후) 활성유저/용량 등 주기 수집 지표로 확장 가능. 단 배정 기준은 count/capacity 가 실용적.

# GameDB 추가 절차
- 새 GameDB(schema, 필요 시 새 인스턴스) 생성 → AccountDB `GameDBRegistry` 에 host/port/db_name 등록(`status='active'`).
- 이후 신규 계정이 부하 인지 배정으로 점차 새 샤드로 들어감.
- 새 샤드가 한동안 비는 게 싫으면(균형) 기존 계정 일부를 아래 "제거/이주" 절차로 옮긴다.

# GameDB 제거 / 계정 이주 절차
- **현재 방식: 전체 서버 중지 후 수행**(라이브 온라인 마이그레이션은 향후).
  1. 제거(또는 이주)할 GameDB 의 계정 식별.
  2. 그 계정들의 데이터를 유효한 GameDB 로 이동(테이블마다 `WHERE account_id=?` 로 추출 — 그래서 모든 테이블에 `account_id` 인덱스).
  3. AccountDB 의 해당 계정 `GameDBIndex` 를 새 값으로 갱신.
- 데이터를 옮겨야 하는 모든 작업(이주/제거/리밸런싱)은 현재 전체 서버 중지 전제 → "라이브 중 `GameDBIndex` 불변" 가정이 성립.

# 커넥션 관리
- 구조(**현재 구현**): 서버당 **`AsyncDBQueue` 1개**가 모든 DB(AccountDB + GameDB 샤드들)를 관리. `(EDBType, dbIndex)` 로 어느 DB인지 지정. (자세한 동작은 아래 "현재 구현 상태(DBConnector)" 참조)
  - **단순 구조**: **공유 worker 풀** + **전역 요청 큐 1개**(모든 DB 공유, Request 가 자기 DbKey 보유) + **DB별 connection 풀**. worker 는 전역 큐 front 를 꺼내 그 요청의 DB 로 실행.
  - **커넥션 수 = DB당 numWorkers 개**(Open 시 전부 미리 연다). worker 는 요청당 커넥션 1개만 점유하고 worker 수 = numWorkers 이므로, 한 DB 로 모든 worker 가 몰려도 **커넥션이 모자랄 수 없음** → cap·runnable 선택·lazy 생성이 전부 불필요.
  - 흐름: 코루틴 → 전역 큐 → worker 가 꺼내 처리 + 콜백 → resume executor(IOCP/Contents 스레드)에서 후속작업.
  - **트레이드오프(격리 없음)**: 전역 큐라 한 DB(샤드)가 느려지면 그 쿼리를 실행 중인 worker 가 묶이고 건강한 DB 요청도 뒤에서 대기할 수 있음(head-of-line). 현재는 모든 샤드가 한 MySQL 인스턴스(다중 스키마)라 격리 이득이 작아 단순함을 택함. **샤드를 별도 호스트로 분리해 격리가 필요해지면 DB별 큐 + DB별 동시 실행 상한(cap) 구조를 재도입**(이전 설계).
- **적정 동시 실행 수(= numWorkers, 곧 DB당 connection 수)**: 앱 워커/유저 수가 아니라 **샤드(MySQL 서버) 용량 기준**.
  - connection 은 동시 실행 쿼리 수를 정함. 필요 동시 connection ≈ **목표 QPS × 평균 쿼리지연**(Little's Law). 예: 1000 QPS × 2ms = 2 → 버스트 여유 포함 4~8.
  - 상한: 샤드 코어/IO 가 동시에 유용하게 처리하는 양. 그 이상은 처리량을 안 올리고 컨텍스트 스위치/락 경합만 늘림(작은 풀이 낫다).
  - **작게 시작(현재 `kNumWorkers=8`) → 측정(큐 대기/쿼리지연/DB CPU·IO) → 조정.**
  - worker 스레드 수는 **공유 풀이라 샤드 수와 무관하게 고정**(전용 worker × 샤드로 폭발하지 않음). 단 **커넥션 총수 = (DB 수) × numWorkers** 로 늘어남에 유의. 더 멀리는 비동기 MySQL I/O 로 쿼리당-스레드 모델 자체 제거 가능(향후).
- (향후 고부하) 게임서버 증설 시 샤드당 총 커넥션 = `G × numWorkers` 가 늘어남 → **ProxySQL(멀티플렉싱)** 으로 상한. MySQL Router 가 아니라 ProxySQL(멀티플렉싱이 핵심 니즈).

# 쓰기 처리량
- **다중 테이블 1트랜잭션 배칭**은 퍼시스턴스 레이어(`DbSaveBatch`)로 구현됨(아래 "퍼시스턴스 레이어" 참조). 한 게임 동작이 여러 행을 바꾸면 한 배치=한 트랜잭션으로 묶는다.
- **단, "메모리 누적 → 주기/임계치 flush(coalescing)"는 하지 않기로 결정**. 한 캐릭터를 한 Stage 스레드가 단독 소유하므로 변경 지점이 명확하고, dirty 추적의 복잡도가 이득보다 크지 않다. "즉시 vs 주기" 같은 저장 정책 분류도 두지 않는다(언제 저장할지는 게임 로직 판단).
- 처리량 확장은 결국 **샤딩**. (잦은 쓰기가 실제로 병목이 되면 그때 저장 스태거/배칭 정책을 재검토.)

# MySQL 설정
- 최소·표준 유지. 단 **`innodb_buffer_pool_size` 는 RAM 의 큰 비율(보통 50~70%)** 로 설정(사실상 필수). 그 외는 기본값 우선.

## 튜닝 후보 (TBD — 지금은 기본값 + buffer_pool 만, 부하 측정 후 조정)
MMO 게임서버(쓰기 빈번 + 내구성/지연 트레이드오프)에서 중요한 것들만. **지금 적용하지 않음.**

| 옵션 | 역할 | MMO 관점 (권장/주의) |
|---|---|---|
| `innodb_buffer_pool_size` | 데이터/인덱스를 RAM 에 캐시 | **가장 중요.** 작업셋이 RAM 에 들어가야 디스크 읽기 회피. RAM 의 50~70%. (위 방침대로 유일하게 미리 잡는 값) |
| `innodb_flush_log_at_trx_commit` | 커밋 시 redo log fsync 정책(내구성↔처리량) | `1`=완전 ACID(커밋마다 fsync, ~ms). `2`=OS 캐시까지만+~1초마다 fsync(정전/OS 크래시 시 **최대 ~1초 손실**, 처리량↑). 일반 게임플레이 쓰기는 `2` 고려, **결제/거래 등 고가치 작업은 `1`**. (전역이지만 세션별 설정도 가능) |
| `sync_binlog` | 바이너리 로그 fsync 정책(복제/HA 켤 때) | `1`=커밋마다 fsync(복제 안전, 느림), `0`/`N`=배치(빠름). 위 옵션과 함께 내구성을 결정. **AccountDB HA/복제 도입 시 같이 결정.** |
| `innodb_redo_log_capacity` (8.0.30+, 구: `innodb_log_file_size`) | redo 로그 총 크기 | 쓰기 많은 MMO 는 **크게** → 체크포인트/플러시 빈도↓ → 쓰기 처리량↑. 대신 크래시 복구 시간↑. |
| `innodb_flush_method` | InnoDB 파일 I/O 방식 | Linux 에선 **`O_DIRECT`** 로 이중 버퍼링(OS 캐시 + buffer pool) 회피. buffer pool 큰 환경에서 권장. |
| `innodb_io_capacity` / `_max` | 백그라운드 flush 속도 상한 | 스토리지 IOPS 에 맞춤. **SSD 면 기본값보다 높게**(과소설정 시 더티페이지 누적 → 쓰기 stall). |
| `max_connections` | 동시 커넥션 상한 | 앱 풀(= DB 수 × numWorkers + 여유)에 맞게. 과도하게 높이면 메모리/컨텍스트 스위치만 늘어남. (고부하 시 ProxySQL 멀티플렉싱으로 백엔드 커넥션 축소) |

- 보조(대개 기본 유지): `innodb_doublewrite`=ON(torn page 방지, 끄지 말 것), SSD 면 `innodb_flush_neighbors`=0, 샤드 스키마가 많으면 `table_open_cache`/`open_files_limit` 상향.
- **원칙**: 내구성(`flush_log_at_trx_commit`/`sync_binlog`)은 "얼마까지 손실 감내 가능한가"의 사업 결정. 나머지(buffer pool/redo/io_capacity)는 **측정(큐 대기·쿼리지연·DB CPU·디스크 IO) 후 조정**. (관련: 위 "쓰기 처리량", 커넥션 수는 "커넥션 관리" 참조)

# 테이블 형태 규약
- 일반적으로: `PK(엔티티ID 단일) + data JSON + created_at + updated_at` + 샤드키(`account_id`) 인덱스.
- `updated_at` 은 트리거 대신 **`ON UPDATE CURRENT_TIMESTAMP`**(MySQL).
- PK 는 계정 단위 저장이면 `(account_id)` 또는 `(account_id, ...)`, 캐릭터/아이템 등 자기 ID 가 있으면 그 ID 단일.

# 현재 물리 스키마

- 전체 현행 DDL은 `DB스키마.md`에 기록한다.
- AccountDB의 현재 테이블은 `Accounts`, `GameDBRegistry`다.
  - `Accounts.login_name`은 로그인 조회와 중복 방지를 위한 관계형 컬럼이다.
  - 비밀번호 해시와 `game_db_index`를 포함한 계정 본문은 `Accounts.data`의 `DataStructures.Account` JSON에 저장한다.
- 모든 활성 GameDB 샤드는 `Characters`, `Currency`, `Item`, `AccountCurrency` 구조를 동일하게 유지한다.
- 게임서버는 `GameDBRegistry`를 읽어 **GameDBIndex → (host, port, db_name)** 토폴로지로 샤드별 커넥션 풀을 구성한다.
- `ItemTransfers`, `ReceivedTransfers` 등 아래 계정 횡단 기능 절의 테이블은 향후 설계이며 현행 스키마가 아니다.

# 계정 횡단 기능 (향후 / TBD)
- 길드 / 우편 / 거래 / 경매 / 친구 / 랭킹 은 계정(샤드) 경계를 넘으므로 **GameDB 샤드에 넣지 않는다.**
- 원칙: **원자성은 한 DB 안에서만 공짜.** 작업이 건드리는 데이터가 한 DB에 모이면 single-DB 트랜잭션, 서로 다른 샤드/DB면 별도 메커니즘 필요. → 글로벌 DB 하나로 "모두" 해결되지 않는다. 두 부류로 나눠서 처리.

## A) 원래 공유 데이터 (관계/목록) → 글로벌 DB에 한 번만
- 친구 그래프, 길드원, 경매 목록, 랭킹처럼 **본질적으로 공유**인 데이터는 샤드에 쪼개 복제하지 말고 **글로벌 DB에 1곳**만 둔다. 그러면 계정 횡단 작업이 **single-DB 트랜잭션**이 되어 원자적.
  - 예) 친구 요청 = 글로벌 `Friendships(account_a, account_b, status)` 에 행 1개 insert/갱신 → 원자적.
- 랭킹은 글로벌 DB 대신 **Redis(Sorted Set)** 도 가능.

## B) 플레이어 간 자산 이동 (거래 / 우편 아이템) → escrow + saga + 멱등성
- 거래/우편-아이템은 **내 인벤(내 샤드) → 상대 인벤/우편(상대 샤드 또는 글로벌 우편)** 으로, **서로 다른 두 스토어**를 바꾼다. 글로벌 DB가 있어도 이건 한 트랜잭션으로 못 묶는다(인벤토리는 각자 샤드에 있음). → **분산 트랜잭션(2PC) 회피, escrow+saga 로 처리.**
- **불변식**: 아이템은 항상 **정확히 1곳**에만 존재(복제·유실 0). 각 단계는 **단일 샤드 로컬 트랜잭션**.

**escrow 테이블 — 출발(내) 샤드에 둔다** (인벤 제거와 원자적이어야 하므로):
```sql
-- 내(출발) 샤드
CREATE TABLE ItemTransfers (
    transfer_id BIGINT      NOT NULL,      -- 전역 유일(snowflake), 멱등 키
    item        JSON        NOT NULL,      -- 보관 중 아이템
    to_account_id  BIGINT      NOT NULL,
    status      VARCHAR(16) NOT NULL,      -- pending / delivered / returned
    created_at  TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (transfer_id),
    KEY idx_status (status)
) ENGINE=InnoDB;
```

**받는 샤드 — 멱등성 마커**(같은 transfer_id 중복 지급 방지):
```sql
-- 상대(받는) 샤드
CREATE TABLE ReceivedTransfers (
    transfer_id BIGINT NOT NULL,
    PRIMARY KEY (transfer_id)
) ENGINE=InnoDB;
```

**saga 흐름**(여러 로컬 트랜잭션 + 실패 시 보상):
```
1. (내 샤드)   인벤 제거 + ItemTransfers 'pending'                 ← 로컬 트랜잭션(원자적)
2. (상대 샤드) ReceivedTransfers 멱등 insert → 새 건이면 인벤에 지급   ← 로컬 트랜잭션
3. (내 샤드)   ItemTransfers 'delivered'(또는 행 삭제)             ← 로컬 트랜잭션
보상: 2가 영구 실패 → 'returned' + 내 인벤 환불                     ← 1단계의 의미적 되돌리기
```

**크래시 복구**(escrow.status 가 saga 상태기록):
- 1 후 죽음 → `pending` → 복구가 2 재실행(멱등이라 안전).
- 2 후 3 전 죽음 → 상대는 이미 받음, `pending` → 2 재실행 시 skip → 3 정리. **중복 없음.**
- 2 영구 실패 → 보상 환불. **항상 1곳 유지.**

- **우편 = 같은 패턴**: 우편 행이 escrow/in-flight 보관소 역할. "내 샤드에서 빼서 → 우편(글로벌/상대샤드)에 넣고 → 상대가 수령(로컬 트랜잭션)". 그래서 거래/우편은 동일 메커니즘.
- saga 구동: 전송 서비스가 단계를 직접 모는 **오케스트레이션** 권장.

## 정리
- **공유 데이터(친구/길드/랭킹/경매)** → 글로벌 DB(또는 Redis)에 1곳, single-DB 트랜잭션.
- **자산 이동(거래/우편 아이템)** → escrow(출발 샤드 테이블) + saga(단계별 로컬 트랜잭션 + 보상) + 멱등성(transfer_id).
- 현재 전부 **미구현(향후)**. 글로벌 DB/스토어 분리 + 위 패턴은 추후 도입.

# AccountDB 가용성 (향후)
- SPOF(모든 로그인 경로) → 향후 **primary + standby(failover) + read replica + GameDBIndex 디렉터리 캐시**. 현재 개발단계라 단일.

# 개발 환경
- 현재 dev 는 **MySQL 8.4 LTS 로컬 설치**(Docker 계획이었으나 로컬로). 클라이언트 라이브러리/헤더는 레포에 vendoring(`Development/Server/mysql/{include,lib}`).
- 멀티샤드 dev 는 한 인스턴스에 여러 스키마(예: `gamedb`, `gamedb_01` …)로 흉내. (향후 Docker 로 다중 인스턴스도 가능)

# 현재 구현 상태 (DBConnector)

구현 위치: `Development/Server/DBConnector` (+ `ServerBase` 가 큐를 소유).

## DBConnection (동기, 커넥션 1개 래퍼)
- `libmysqlclient` 기반. `mysql_init` + `mysql_real_connect`. 옵션: `utf8mb4`, `MYSQL_OPT_GET_SERVER_PUBLIC_KEY`(caching_sha2_password 를 비-TLS 에서도 사용).
- 쿼리는 **prepared statement**(`mysql_stmt_*`) + 파라미터 바인딩(`DBParam` = NULL/int64/double/string/blob). 결과는 0-length 바인딩 + `fetch_column` 으로 가변길이 안전하게 읽고, 필드 타입별로 `DBValue`(int64/double/string/blob) 변환.
- 연결 실패 시 `mysql_errno`+메시지를 `GetLastError()` 로 노출(핸들 닫은 뒤에도 보관).
- `DBConnection.cpp` 에 `#pragma comment(lib, "libmysql.lib")` → DBConnector 를 링크하는 모든 exe 가 자동으로 libmysql 링크(라이브러리 경로 `mysql\lib` 는 각 vcxproj 의 LibraryPath).

## AsyncDBQueue (서버당 1개, 모든 DB 통합 관리)
- 한 클래스가 **AccountDB + GameDB 샤드 + 기타 DB 전부**를 관리. DB 식별 = **`(EDBType, int dbIndex)`**.
  - `enum class EDBType { Account, Game }`. `Account` 는 `dbIndex` 무시, `Game` 은 `dbIndex`(=`game_db_index`)로 샤드 선택. (내부 맵 키 = `std::pair<EDBType,int>`, Account 는 index 0 으로 정규화)
- 공개 쿼리 API 는 **`ExecuteAsync(EDBType, dbIndex, query, params, executor)` 하나**(코루틴 `co_await`). 콜백형 `Execute` 는 통합·제거됨.
- 내부 구조(단순): **공유 worker 풀** + **전역 요청 큐 1개**(`deque`, 모든 DB 공유, Request 가 DbKey 보유) + **DB별 connection 풀**. worker 는 전역 큐 front 를 꺼내 요청의 DB 커넥션으로 실행.
  - 각 DB 는 **numWorkers 개 커넥션을 Open 시 전부 미리 연다**. worker 수 = numWorkers, worker 는 요청당 커넥션 1개만 점유 → 한 DB 로 모든 worker 가 몰려도 유휴 커넥션이 항상 남음(부족 불가). 그래서 cap·runnable 선택·lazy 생성이 전부 불필요.
- `Open(entries, numWorkers)` — `entries` = `(EDBType, index, DBConnectionConfig)` 목록. 각 DB 의 커넥션 numWorkers 개를 미리 열어 연결성 검증. 단일 DB 편의 오버로드 `Open(config, ...)`(Account 1개) 도 있음.
- 종료(`Close`)는 전역 큐의 잔여 요청을 드레인한 뒤 worker 조인.

### 모니터링
- ServerBase가 `IAsyncDBMetricsSink`를 주입하면 AsyncDBQueue는 요청의 enqueue, worker 시작, 완료 경계만 통지한다. DBConnector는 Prometheus 구현을 의존하지 않는다.
- `db_type=account|game`, `operation=query|transaction`의 고정 label로 request/result/rejected, queue depth, active request를 집계한다.
- enqueue부터 worker 시작까지는 `mmo_db_queue_wait_seconds`, 실제 job 실행 구간은 `mmo_db_execution_seconds`로 분리한다. transaction의 실행시간에는 재시도와 backoff가 포함된다.
- SQL, table, accountId, dbIndex와 error message는 label로 기록하지 않는다.
- 모니터링을 사용하지 않으면 sink가 null이므로 요청별 `steady_clock` 측정을 건너뛴다.

## 코루틴 / 스레드 모델
- `ExecuteAsync` 는 `DBResultAwaitable` 반환. worker 가 쿼리 완료 후 콜백 → `IResumeExecutor`(`Post`)가 후속작업 재개 스레드를 결정.
  - IOCP 워커에서 재개: `ServerBase::GetCoroutineResumeExecutor()`. Stage 컨텐츠 스레드에서 재개: 해당 `ContentsThread` 의 executor(Stage 단일스레드 불변식 유지 + `AsyncPin` 수명게이트).

## ServerBase 가 DB 소유 + 자동 초기화
- `ServerBase::m_dbQueue`(서버당 1개). `Initialize` 가 `OnInitialize` **전에** `initializeDatabases()` 호출 → `ServerBaseConfig` 의 DB 설정대로 연결. 서브클래스는 `GetDB()` 로 접근. 종료 시 ServerBase 가 닫는다.
- 부트스트랩: AccountDB 연결 → (GameDB 사용 시) 임시 `DBConnection` 으로 **`GameDBRegistry`(status='active') 를 읽어 샤드들을 등록** → `m_dbQueue.Open(...)` 한 번에 모든 DB 오픈.

## ini 설정 (`LoadDBConfigFromIni`)
```ini
[AccountDB]                 # Host 가 있으면 AccountDB 사용
Host=127.0.0.1
Port=3306
User=root
Password=...
DBName=accountdb

[GameDB]                    # Enabled=true 면 GameDB 샤딩 사용 (AccountDB 필수)
Enabled=true
```
- **GameDB 샤드의 host/port/db 는 `GameDBRegistry` 에서 읽고, 접속 자격증명(user/password)은 AccountDB 와 공유**(완전 공유로 단순화). LoginServer 는 `[AccountDB]` 만, GameServer 는 `[AccountDB]`+`[GameDB]`.

## 트랜잭션 API (구현됨)
- **`TransactionAsync(EDBType, dbIndex, body, executor, opts)`** — 한 커넥션에서 `BEGIN…(body)…COMMIT` 으로 묶고 일시적 에러는 본문을 재실행한다. (위 "트랜잭션" 섹션 설계대로)
- **모델**: 본문(`TxBody = bool(DBTransaction&)`)은 worker 스레드에서 **동기로, 재시도 시 여러 번** 실행. co_await 은 트랜잭션 전체에 1번. → 커넥션이 본문 도는 동안만 점유(1개 고정), 재시도는 본문 재호출(replay)이라 단순. "짧게 유지" 충족.
  - `DBTransaction::Execute` 로 같은 트랜잭션 안에서 여러 쿼리. 쿼리 실패 시 내부에 `errorCode` 기록.
  - 본문 반환 `true`=커밋 의도 / `false`=롤백(비즈니스 중단, 재시도 안 함).
- **재시도 분류**: 일시적(1213 deadlock/1205 lock timeout/2006·2013 연결끊김)만 재시도(`maxAttempts=4`, 백오프 base 5ms + 지터). 연결끊김은 `Open(cfg)` 재연결 후 재시도. 결정적 에러(1062 중복키 등)는 **즉시 실패**.
- **결과 해석**: `success==true`=커밋됨 / `success==false && errorCode==0`=비즈니스 롤백 / `success==false && errorCode!=0`=DB에러(재시도 소진 포함).
- **본문 작성 규칙(중요)**: worker 스레드 + replay 이므로 **순수 DB 작업만**(게임상태 접근·패킷전송 등 부수효과 금지). 출력은 캡처 지역변수에 쓰되 `success==true` 일 때만 유효. 락 순서 일관은 호출부 책임.
- 구현: `DBConnection::Begin/Commit/Rollback`(text 프로토콜) + `DBResult.errorCode`(mysql_errno) + `AsyncDBQueue::runTransaction`. Request 가 `job(DBConnection&, config)` 으로 일반화되어 worker 는 단발/트랜잭션을 구분 안 함.

## 게임서버 사용 패턴
- 입장/리라우팅 시 `loadAccount(accountId)` 로 **`DataStructures::Account` 전체**를 읽어 `User` 에 보관(`User::m_account`). `User::GetGameDbIndex()` 는 거기서 파생.
- 캐릭터 조회/생성/저장: `GetDB().ExecuteAsync(EDBType::Game, user.GetGameDbIndex(), ...)`. 계정 조회: `GetDB().ExecuteAsync(EDBType::Account, 0, ...)`.
- 다중 행 원자 변경(인벤토리/Gold 등): `GetDB().TransactionAsync(EDBType::Game, user.GetGameDbIndex(), body, executor)`.

## 검증 상태
- **단일 샤드(인덱스 0)** 로 로그인(AccountDB) → 게임 접속 → 캐릭터 목록/선택/생성/저장, 크로스서버 이동까지 동작 확인. (멀티샤드는 배정 로직 미구현이라 미검증)

# 퍼시스턴스 레이어 (배치 저장)

게임로직이 **여러 테이블 구조체를 배치에 모아 던지면 한 계정 샤드 안에서 단일 트랜잭션으로 upsert/delete** 되는 레이어(읽기는 멀티문장 1왕복 배치). 손으로 SQL 쓰던 방식을 대체.

## 위치 / 구성
- **DBConnector 프로젝트, `namespace db`** (DB 기능은 DBConnector에 둔다).
  - `DbTables.h` — 테이블 레지스트리. `EDbTable` enum + `DbTable<T>` trait(proto 타입↔테이블, 메타 `kInfo`, 쓰기 키추출 `IdColumns`, 읽기 키 `kLoadKeyCol`/`LoadKey`) + `GetDbTableInfo` 카탈로그. **자동생성 대상**(현재 수기, 향후 PacketGenerator).
  - 쓰기: `DbSaveBatch.h`(배치 컨테이너, `make_shared` 공유 누적) + `DbSaveExecutor.h/.cpp`(한 트랜잭션 upsert/delete).
  - 읽기: `DbLoadBatch.h`(로드 요청 누적) + `DbLoadResult.h`(파싱된 proto 보관) + `DbLoadExecutor.h/.cpp`(멀티문장 1왕복 + 워커 스레드 역직렬화).
- DBConnector가 게임 proto(`DataStructures`)+`ProtoJsonSerializer`(PacketGenerator)에 의존하게 됨(순환 아님).

## 핵심 설계 결정
- **proto는 `shared_ptr` 로 보유**, JSON **직렬화는 write 직전(worker 스레드)** 에 재사용 버퍼로. DB 성공 후 배치에서 proto를 꺼내 메모리 반영(readback)할지는 호출자 자유(DB-first).
- **upsert/delete 동일 구조** + **고정 시그니처** `Upsert/Delete(proto, accountId, characterId)`. 어떤 키를 쓸지는 각 테이블 `IdColumns`가 고름(호출부는 키 구성 몰라도 됨). 읽기도 대칭: `DbLoadBatch::Load/LoadMany(accountId, characterId)` + 테이블별 `LoadKey`가 WHERE 키 선택.
- **배치 내 PK 중복검사** → 경고 로그 + 교체(last-wins). delta 누적 안 함.
- **데드락 회피**: 테이블 = `EDbTable` 선언순(`std::map` 키), 같은 테이블 행 = PK 오름차순(`RowMap` 자동 정렬). cross-batch 동시성은 게임 로직 책임(busy 마킹 안 함).
- **dirty 추적 / 즉시·주기 분류 없음**(위 "쓰기 처리량" 참조).
- 멀티행 `INSERT ... ON DUPLICATE KEY UPDATE` + `DELETE`(단일 PK=IN / 복합 PK=row-constructor IN). 복합 PK 구조는 열어둠(현재 전부 단일).
- **읽기는 `DbLoadExecutor`**: 여러 테이블 SELECT를 **멀티문장 1왕복** + 한 트랜잭션(스냅샷 일관) + **DB 워커 스레드에서 proto로 역직렬화**(게임로직 스레드 파싱 0). 키가 정수라 SQL 직접 삽입(인젝션 없음).
- **dirty 추적 없음**: 한 캐릭터 = 한 Stage 스레드 단독 소유라 변경 지점이 게임로직에 명확 → 변경 그 자리에서 배치 저장. "즉시/주기" 정책 분류도 없음(언제 저장할지는 게임로직 판단). (위 "쓰기 처리량" 참조)

## 데드락 회피 규칙 (다중테이블 트랜잭션)
두 트랜잭션이 같은 자원을 반대 순서로 잠그면 순환 대기=데드락. 원칙: **모든 트랜잭션이 자원을 같은 전역 순서로 접근.** 이 레이어가 강제한다:
1. **테이블 순서 = `EDbTable` 선언순** (`DbSaveBatch`가 `std::map<EDbTable,..>` → 자동). `unordered_map` 금지(순회 비결정 → 데드락 위험).
2. **같은 테이블 행 순서 = PK 오름차순** (`RowMap`이 PK 튜플 키 `std::map` → 순회가 자동 오름차순).
3. **단계 순서 = upsert → delete** (`DbSaveExecutor` 고정).
4. 배치를 안 거치는 직접 `TransactionAsync`도 **같은 테이블 전역 순서 준수** → 가능하면 모든 다중테이블 쓰기를 배치 경로로 단일화해 한 곳에서 보장.
5. 그래도 남는 데드락(InnoDB 갭락 등)은 `TransactionAsync` 재시도가 흡수(1213/1205 분류 → 백오프 replay).

## 설계 결정 / 미결
- **런타임 모델 위치**: 캐릭터 스코프(인벤/재화)는 `Character` 하위, **계정 스코프(AccountCurrency 등)는 `User` 하위**(캐릭터 소멸/전환과 무관해야 하므로). 현재는 임시로 proto 보관, 런타임 컴포넌트화는 향후.
- **(미결) HP/MP/버프 영속 여부**: 현재 비영속(이동 시 잡 기본값 리셋). 영속 필요 시 정책 결정.
- **(미결) `DbTables.h` 자동생성 전환 / 히스토리 도입(라이브 전) 시점**.

## 알려진 위험: co_await 창의 TOCTOU (향후 대응, 지금 미개발)
- **패턴 전제**: DB-first 저장(① 메모리에서 조건 체크 → ② proto 복사·변경분 적용 → ③ 배치 Save `co_await` → ④ 성공 시 메모리 반영). 이 순서 자체는 DB 실패 시 메모리/DB 불일치가 없어 옳다(특히 구매/거래 같은 dupe 민감 작업).
- **위험**: Stage 는 단일 스레드지만 `co_await` 으로 suspend 되면 그 스레드가 **다른 메시지를 처리**한다. 그래서 ①(체크)와 ④(반영) 사이에 같은 캐릭터의 골드/인벤이 바뀔 수 있다(TOCTOU). 같은 캐릭터로 변경 요청 2개가 겹치면 둘 다 체크를 통과하고, 재화 proto 는 **통째 JSON last-write-wins** 라 골드는 한 번만 차감되고 아이템은 두 번 지급되는 **dupe** 가 가능. (await 사이 바뀐 *다른 필드*도 통째 교체로 덮어씀)
- **대응(향후)**: 변경 핸들러 진입부에서 **캐릭터당 pending-async 가드로 직렬화**한다. `Character::HasPendingAsync()` 가 이미 있고 스테이지 이동 거부에 사용 중(`Stage.cpp` `handleClientStageMoveReq`) — 같은 가드를 골드/인벤 변경 핸들러에도 적용해 async 진행 중이면 거부(클라 재시도) 또는 큐잉. 이 가드가 있어야 ④의 메모리 반영도 안전(반영 대상이 그새 드리프트하지 않음).
- 참고: 고빈도 상태(이동/전투/HP)는 이 동기 DB-first 가 아니라 인메모리 즉시 반영 + 비동기 write-back 트랙으로 가야 함(DB 지연이 게임 응답 경로에 끼지 않도록).

## 구현 상태 (done)
- 레지스트리 + 쓰기 배치/실행기 **구현·빌드 완료**(DBConnector). 테이블: Characters/Currency/Item/AccountCurrency.
- `saveCharacterToDB` 를 **배치 경로로 교체**(`UPDATE` → `INSERT...ON DUPLICATE KEY UPDATE`). 크로스서버 이동 저장도 배치 사용.
- **읽기 레이어**(`DbLoadBatch`/`DbLoadResult`/`DbLoadExecutor`) 구현 — 멀티문장 1왕복 + 워커 스레드 역직렬화.
- `loadCharacterForUser` 가 **캐릭터+재화+아이템+계정재화를 한 번에 로드**(재화/아이템→`Character`, 계정재화→`User` 에 임시 proto 보관).
- **손쿼리 → 퍼시스턴스 레이어 흡수**: `loadAccount`(Accounts, AccountDB)·캐릭터 목록(Characters)·캐릭터 생성(Characters)을 `DbLoadExecutor`/`DbSaveExecutor` 로 교체. 손쿼리 5→2(남은 건 로그인 `by login_name`, 부트스트랩 `GameDBRegistry` — 각각 사용자입력 바인딩/관계형 부트스트랩이라 의도적으로 손쿼리 유지).
- `DbLoadExecutor::Load` 에 **`EDBType` 파라미터** 추가 → AccountDB/GameDB 둘 다 타겟(`Accounts` 는 로드 전용으로 레지스트리 등록, `IdColumns` 미정의 = 저장 차단).
- **정적 스키마 통합**: 로드 WHERE 컬럼을 `DbTableInfo.loadKeyCol` 로 이동 → 쓰기 `insertCols` ↔ 읽기 `loadKeyCol` 대칭(컬럼명=kInfo, 값=`IdColumns`/`LoadKey` 함수).
- DBConnection 에 **커넥션별 prepared statement 캐시**(반복 쿼리 prepare 왕복 절약).
- 테스트 upsert(`UpsertTestCurrencyAndItemFromStage`)가 배치로 동작.
- ※ 런타임 실검증은 아직(컴파일 + 기존 경로 대체로만 확인). 실제 인벤/Gold 테이블·게임로직 붙으면 검증.

## 남은 작업 (TODO)
- **수명 경계 저장(유실 방지)** — `handleGatewayUserDisconnect`(로그아웃/접속종료)에 Character 저장 추가(현재 제거만), 서버종료 시 전 Character 저장. *가장 우선*.
- **아이템/재화 변경 시 저장 경로 + 런타임 모델** — 인벤토리/재화 런타임 보관 모델(현재 없음, `Character` 하위 컴포넌트로 결정) + 변경 지점에서 배치 저장. 계정 스코프 데이터(AccountCurrency 등)는 `User` 하위.
- **로드 일반화** — 캐릭터 번들 로드(캐릭선택/재라우팅)·계정 입장 캐릭터 *목록*(`handleGatewayUserEnter`)·계정 로드(`loadAccount`) **모두 `DbLoadExecutor` 로 완료**. 잔여: 재화/아이템 **런타임 컴포넌트화**(현재 임시 proto 보관만).
- **(선택) 히스토리 테이블**(아이템 감사/복구), **PacketGenerator로 `DbTables.h` 자동생성**(proto custom option).

# 향후 / 미결 항목
- ~~**트랜잭션 API**~~ — **구현 완료**(위 "트랜잭션 API (구현됨)" 섹션). 실사용 검증은 인벤토리/Gold 등 실제 테이블 생기면.
- **계정 생성 + `game_db_index` 배정** — 회원가입(Accounts INSERT) 흐름 + `GameDBRegistry.account_count/weight` 기반 부하인지 배정(+`FOR UPDATE`). 현재는 계정 수동 INSERT(샤드 0 고정). **미구현** → 멀티샤드 실검증의 선결조건.
- **비밀번호 해시** — LoginServer 가 현재 평문 비교(`password != login_password_hash`). 해시 검증(bcrypt/argon2 등)으로 교체 필요.
- **DLL 레포 벤더링 + post-build 복사** — `libmysql.dll`/`libssl-3-x64.dll`/`libcrypto-3-x64.dll` 을 레포(`mysql/bin`)에 두고 빌드 시 OUTPUT 으로 자동 복사. 현재는 OUTPUT\Debug 에 수동 복사.
- **serverId 전역 유일 할당 절차** — snowflake ID 충돌 방지(설정/레지스트리).
- **스키마 DDL 의 N개 샤드 적용/변경 도구** — 스키마 변경 시 모든 GameDB 에 동시 반영(마이그레이션 스크립트).
- **온라인 마이그레이션(무중단 이주)** — 현재는 전체 서버 중지 방식.
- ~~**다중테이블 배치 저장**~~ — **구현 완료**(위 "퍼시스턴스 레이어"). coalescing/스태거는 채택 안 함(필요 시 재검토).
- **퍼시스턴스 레이어 잔여** — 수명 경계 저장(로그아웃/종료), 아이템·재화 런타임 모델 + 변경 시 저장, 로드 일반화, (선택)히스토리/자동생성. (위 "퍼시스턴스 레이어 — 남은 작업" 참조)
- **AccountDB HA** — 미구현(개발단계).
- **계정 횡단 글로벌 DB / 랭킹(Redis) + escrow·saga 구현** — TBD.
- **ProxySQL** — 고부하 단계에서 도입.
