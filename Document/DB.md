# DB 종류
- DB 종류
	- SQLite
	- 참고: SQLite는 원격으로 연결하여 사용하기 힘들기 때문에 게임서버의 DB로는 적합하지 않습니다. 하지만 지금은 개발단계이므로 일단 SQLite를 사용하고, 나중에 다른 RDB로 교체할 예정입니다.
	
# 데이터 저장 형태
- DB에 저장되는 데이터는 모두 json string 형식으로 저장됩니다.
- DB에 저장되는 데이터 구조체는 PacketGenerator 프로젝트의 Proto/DataStructures 폴더에 .proto 파일로 정의되어 있습니다. 실제 코드 파일은 PacketGenerator 프로젝트의 Generated/DataStructures 폴더에 .pb.cc, .pb.h 파일로 있습니다.
- DB에 데이터를 저장할 때는 PacketGenerator 프로젝트의 ProtoJsonSerializer::ToJson 함수로 json string으로 변환하여 저장하고, DB에서 데이터를 읽을 때는 ProtoJsonSerializer::FromJson 함수로 데이터 구조체로 변환하여 사용합니다.

# DB 테이블 형태
- 데이터를 json string으로 저장하기 때문에, 일반적으로 DB 테이블은 `PK컬럼(1개이상), 데이터컬럼(1개), 생성일자, 업데이트일자` 컬럼을 가집니다.
- 여기서 생성일자, 업데이트일자는 자동 생성 데이터입니다.
- PK는 데이터의 저장단위가 계정일 경우에는 AccountId, 데이터의 저장단위가 캐릭터일 경우에는 AccountId + CharacterId 입니다. 데이터 저장단위에 따라 다른 PK 형태가 있을수도 있습니다.
- 추가로 인덱스용 컬럼 등이 추가될 수 있습니다.

# DB
- Account
	- 계정 관련 데이터가 저장됩니다.
- Game
	- 게임플레이 관련 데이터가 저장됩니다.
	
# Account DB
(필요에 따라 계속해서 수정/추가 예정)

## User (계정)
CREATE TABLE IF NOT EXISTS Users (
    user_id      INTEGER NOT NULL,
    login_name TEXT NOT NULL UNIQUE
    login_password_hash TEXT NOT NULL,
    created_at   INTEGER NOT NULL DEFAULT (CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)),
    updated_at   INTEGER NOT NULL DEFAULT (CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)),
    PRIMARY KEY (user_id)
);

CREATE UNIQUE INDEX idx_login_name ON Users (login_name);

CREATE TRIGGER IF NOT EXISTS Update_Users_UpdatedAt
AFTER UPDATE ON Users
FOR EACH ROW
BEGIN
    UPDATE Users 
    SET updated_at = CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)
    WHERE user_id = OLD.user_id;
END;

# GameDB
(필요에 따라 계속해서 수정/추가 예정)

## Characters
CREATE TABLE IF NOT EXISTS Characters (
    user_id      INTEGER NOT NULL,
    character_id INTEGER NOT NULL,
    data         TEXT    NOT NULL,
    created_at   INTEGER NOT NULL DEFAULT (CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)),
    updated_at   INTEGER NOT NULL DEFAULT (CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)),
    PRIMARY KEY (user_id, character_id)
);

CREATE TRIGGER IF NOT EXISTS Update_Characters_UpdatedAt
AFTER UPDATE ON Characters
FOR EACH ROW
BEGIN
    UPDATE Characters 
    SET updated_at = CAST(ROUND(unixepoch('subsecond') * 1000) AS INTEGER)
    WHERE user_id = OLD.user_id AND character_id = OLD.character_id;
END;

