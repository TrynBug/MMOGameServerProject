-- ────────────────────────────────────────────────────────────────
-- GameDB Schema
-- ────────────────────────────────────────────────────────────────
-- GameServer가 사용하는 DB.
--
-- 설계 원칙:
-- - 게임 데이터는 protobuf 메시지를 JSON 문자열로 직렬화하여 TEXT 컬럼에 저장한다.
-- - 키와 메타데이터(생성/수정 시각 등)만 별도 컬럼으로 둔다.
-- - 이렇게 하면 게임 데이터 구조가 바뀌어도 DB 스키마는 그대로 유지된다.
--
-- 적용 방법:
--   1) Common/init_gamedb.bat 실행, 또는
--   2) sqlite3 GameDB.db < GameDBSchema.sql
-- ────────────────────────────────────────────────────────────────

-- 캐릭터 (DataStructures.Character protobuf 메시지를 JSON으로 저장)
-- 1명의 유저는 여러개의 캐릭터를 가질 수 있다.
-- PK는 (user_id, character_id). character_id는 ObjectIdGenerator로 발급된 영구 ID.
DROP TABLE IF EXISTS Characters;

CREATE TABLE Characters (
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