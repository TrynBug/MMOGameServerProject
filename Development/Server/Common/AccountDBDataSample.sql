-- ────────────────────────────────────────────────────────────────
-- AccountDB Sample Data
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
--   1) Common/init_accountdb.bat 실행 (스키마 적용 후 이 파일도 함께 적용됨), 또는
--   2) sqlite3 AccountDB.db < AccountDBDataSample.sql
-- ────────────────────────────────────────────────────────────────

INSERT INTO Users (user_id, login_name, data) VALUES (
    10000001,
	"test",
    '{"user_id":"10000001","login_name":"test","login_password_hash":"test"}'
);

INSERT INTO Users (user_id, login_name, data) VALUES (
    10000002,
	"test2",
    '{"user_id":"10000002","login_name":"test2","login_password_hash":"test2"}'
);


INSERT INTO Users (user_id, login_name, data) VALUES (
    10000003,
	"test3",
    '{"user_id":"10000003","login_name":"test3","login_password_hash":"test3"}'
);


INSERT INTO Users (user_id, login_name, data) VALUES (
    10000004,
	"test4",
    '{"user_id":"10000004","login_name":"test4","login_password_hash":"test4"}'
);