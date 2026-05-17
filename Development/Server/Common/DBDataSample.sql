-- ────────────────────────────────────────────────────────────────
-- GameDB Sample Data
-- ────────────────────────────────────────────────────────────────
-- 개발/테스트용 샘플 캐릭터.
--
-- JSON 형식 주의사항:
-- - protobuf의 ProtoJsonSerializer 설정: preserve_proto_field_names=true (snake_case 유지),
--   always_print_fields_with_no_presence=true (기본값 필드도 출력).
-- - int64는 protobuf JSON 표준상 문자열로 표현되지만, 입력 시에는 숫자도 허용된다.
--   여기서는 일관성을 위해 문자열로 적는다.
-- - 새 user_id에 샘플을 추가하려면 동일한 형식을 따른다.
--
-- 적용 방법:
--   1) Common/init_gamedb.bat 실행 (스키마 적용 후 이 파일도 함께 적용됨), 또는
--   2) sqlite3 GameDB.db < DBDataSample.sql
-- ────────────────────────────────────────────────────────────────

-- user_id=1: 기본 위치, 레벨 1
INSERT OR IGNORE INTO Characters (user_id, data) VALUES (
    1,
    '{"user_id":"1","name":"Alice","level":1,"exp":"0","hp":100,"max_hp":100,"mp":50,"max_mp":50,"last_stage_id":1,"pos_x":0,"pos_y":0,"pos_z":0,"dir_y":0}'
);

-- user_id=2: 약간 레벨업한 캐릭터
INSERT OR IGNORE INTO Characters (user_id, data) VALUES (
    2,
    '{"user_id":"2","name":"Bob","level":5,"exp":"1200","hp":180,"max_hp":180,"mp":80,"max_mp":80,"last_stage_id":1,"pos_x":10,"pos_y":0,"pos_z":5,"dir_y":1.5708}'
);

-- user_id=3: 다른 위치
INSERT OR IGNORE INTO Characters (user_id, data) VALUES (
    3,
    '{"user_id":"3","name":"Charlie","level":3,"exp":"500","hp":140,"max_hp":140,"mp":65,"max_mp":65,"last_stage_id":1,"pos_x":-15,"pos_y":0,"pos_z":-10,"dir_y":3.14159}'
);
