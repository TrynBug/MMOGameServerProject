# DB 스키마

이 문서는 현재 개발 환경의 MySQL 스키마를 기록한다. DB 구성·샤딩·접근 방식·퍼시스턴스 정책은 `DB.md`를 참조한다.

## 기준과 갱신 규칙

- 기준 DBMS: MySQL 8.4, InnoDB
- 확인 기준일: 2026-07-30
- 확인 방법: 로컬 개발 DB의 `SHOW CREATE TABLE`
- AccountDB 스키마명: `accountdb`
- 활성 GameDB 스키마: `GameDBRegistry`의 `status='active'` 행이 가리키는 스키마. 확인 당시 `gamedb` 1개.
- 로컬 Windows MySQL은 테이블명을 소문자로 저장하므로 아래 DDL은 `SHOW CREATE TABLE`에 표시된 물리 이름을 그대로 쓴다. 서버 코드의 `Accounts`, `Characters` 같은 논리 이름은 이 환경에서 대소문자를 구분하지 않고 같은 테이블을 가리킨다.
- 테이블 구조를 변경할 때는 실제 DB, `DBConnector/DbTables.h`, 관련 protobuf, 이 문서를 같은 작업에서 함께 갱신한다.
- 이 문서는 스키마 현황 문서이며 DB 생성·마이그레이션 도구를 대신하지 않는다. 현재 저장소에는 MySQL 마이그레이션 도구가 없다.

## 공통 저장 규약

- 영속 데이터 본문은 protobuf 메시지를 JSON으로 직렬화해 `data JSON` 컬럼에 저장한다.
- 저장은 `ProtoJsonSerializer::ToJson`, 로드는 `ProtoJsonSerializer::FromJson`을 사용한다.
- `created_at`과 `updated_at`은 MySQL이 관리한다.
- GameDB의 계정 소유 데이터는 계정 단위 조회·이주를 위해 `account_id`를 별도 컬럼 또는 인덱스에 포함한다.

| 테이블 | `data` protobuf | 소유 범위 |
|---|---|---|
| `Accounts` | `DataStructures.Account` | 계정 |
| `Characters` | `DataStructures.Character` | 캐릭터 |
| `Currency` | `DataStructures.Currency` | 캐릭터 |
| `Item` | `DataStructures.Item` | 캐릭터의 아이템 1개 |
| `AccountCurrency` | `DataStructures.AccountCurrency` | 계정 |

`GameDBRegistry`는 라우팅에 필요한 관계형 데이터이므로 JSON 본문을 사용하지 않는다.

# AccountDB

## Accounts

- 로그인 이름은 빠른 조회와 중복 방지를 위해 관계형 컬럼으로도 보관한다.
- 비밀번호 해시와 `game_db_index`를 포함한 계정 본문은 `data`의 `DataStructures.Account`에 저장한다.

```sql
CREATE TABLE `accounts` (
  `account_id` bigint NOT NULL,
  `login_name` varchar(64) NOT NULL,
  `data` json NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  UNIQUE KEY `uk_login_name` (`login_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## GameDBRegistry

- GameServer는 `status='active'`인 행을 읽어 GameDB 샤드별 연결 정보를 구성한다.
- `account_count`와 `weight`는 향후 계정 생성 시 샤드 배정에 사용할 값이다.

```sql
CREATE TABLE `gamedbregistry` (
  `game_db_index` int NOT NULL,
  `host` varchar(255) NOT NULL,
  `port` int NOT NULL DEFAULT '3306',
  `db_name` varchar(64) NOT NULL,
  `status` varchar(16) NOT NULL DEFAULT 'active',
  `account_count` bigint NOT NULL DEFAULT '0',
  `weight` int NOT NULL DEFAULT '1',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`game_db_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

# GameDB

모든 활성 GameDB 샤드는 아래 테이블 구조를 동일하게 유지해야 한다.

## Characters

```sql
CREATE TABLE `characters` (
  `character_id` bigint NOT NULL,
  `account_id` bigint NOT NULL,
  `data` json NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_id`),
  KEY `idx_account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## Currency

캐릭터당 최대 1행이며 `character_id`가 PK다.

```sql
CREATE TABLE `currency` (
  `character_id` bigint NOT NULL,
  `account_id` bigint NOT NULL,
  `data` json NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_id`),
  KEY `idx_account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## Item

아이템 인스턴스 1개가 1행이다. 캐릭터별 로드는 `character_id`, 계정 단위 이주는 `(account_id, character_id)` 인덱스를 사용한다.

```sql
CREATE TABLE `item` (
  `item_id` bigint NOT NULL,
  `character_id` bigint NOT NULL,
  `account_id` bigint NOT NULL,
  `data` json NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`item_id`),
  KEY `idx_owner` (`account_id`,`character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## AccountCurrency

계정당 최대 1행이며 `account_id`가 PK다.

```sql
CREATE TABLE `accountcurrency` (
  `account_id` bigint NOT NULL,
  `data` json NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## 아직 현행 스키마가 아닌 항목

`ItemTransfers`, `ReceivedTransfers`, 친구, 길드, 우편, 거래, 경매, 랭킹 관련 테이블은 `DB.md`의 향후 설계이며 현재 개발 DB에는 존재하지 않는다.
