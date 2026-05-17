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
--   2) sqlite3 GameDB.db < DBSchema.sql
-- ────────────────────────────────────────────────────────────────

-- 캐릭터 (DataStructures.Character protobuf 메시지를 JSON으로 저장)
CREATE TABLE IF NOT EXISTS Characters (
    user_id       INTEGER PRIMARY KEY,
    data          TEXT    NOT NULL,
    last_updated  INTEGER NOT NULL DEFAULT (strftime('%s','now') * 1000)
);
