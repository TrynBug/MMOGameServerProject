-- DummyClient용 계정 초기화/생성 스크립트 (MySQL 8)
--
-- 전제:
--   - AccountDB와 대상 GameDB schema가 같은 MySQL 인스턴스에 있다.
--   - 현재 테이블 구조는 Accounts / GameDBRegistry / Characters / Currency / Item / AccountCurrency이다.
--   - 실행 전에 Login/Gateway/GameServer와 DummyClient를 모두 종료한다.
--
-- 기본 CALL은 p_apply=FALSE라 조회만 한다.
-- 조회 결과를 확인한 뒤 마지막 CALL의 FALSE를 TRUE로 바꿔 다시 실행한다.

USE accountdb;

DROP PROCEDURE IF EXISTS ResetDummyAccounts;

DELIMITER //

CREATE PROCEDURE ResetDummyAccounts(
    IN p_apply BOOLEAN,
    IN p_prefix VARCHAR(32),
    IN p_count INT,
    IN p_account_id_base BIGINT,
    IN p_target_game_db_index INT
)
main: BEGIN
    DECLARE v_game_db_name VARCHAR(64);
    DECLARE v_existing_other_shards INT DEFAULT 0;
    DECLARE v_id_collisions INT DEFAULT 0;
    DECLARE v_index INT DEFAULT 1;
    DECLARE v_account_id BIGINT;
    DECLARE v_login_name VARCHAR(64);

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    IF p_prefix IS NULL OR p_prefix = '' OR p_prefix NOT REGEXP '^[A-Za-z0-9_]+$' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_prefix must contain only letters, digits, or underscore';
    END IF;
    IF p_count <= 0 OR p_count > 100000 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'p_count must be between 1 and 100000';
    END IF;

    SELECT db_name
      INTO v_game_db_name
      FROM GameDBRegistry
     WHERE game_db_index = p_target_game_db_index
       AND status = 'active'
     LIMIT 1;

    IF v_game_db_name IS NULL OR v_game_db_name = '' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'active target GameDB was not found';
    END IF;

    DROP TEMPORARY TABLE IF EXISTS tmp_dummy_accounts;
    CREATE TEMPORARY TABLE tmp_dummy_accounts (
        account_id BIGINT NOT NULL PRIMARY KEY,
        login_name VARCHAR(64) NOT NULL,
        game_db_index INT NOT NULL
    );

    INSERT INTO tmp_dummy_accounts (account_id, login_name, game_db_index)
    SELECT account_id, login_name,
           COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(data, '$.game_db_index')) AS SIGNED), 0)
      FROM Accounts
     WHERE login_name REGEXP CONCAT('^', p_prefix, '[0-9]+$');

    SELECT COUNT(*)
      INTO v_existing_other_shards
      FROM tmp_dummy_accounts
     WHERE game_db_index <> p_target_game_db_index;

    IF v_existing_other_shards > 0 THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'dummy accounts exist on another shard; delete them from that GameDB before running this script';
    END IF;

    SELECT COUNT(*)
      INTO v_id_collisions
      FROM Accounts
     WHERE account_id BETWEEN p_account_id_base + 1 AND p_account_id_base + p_count
       AND login_name NOT REGEXP CONCAT('^', p_prefix, '[0-9]+$');

    IF v_id_collisions > 0 THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'planned dummy account_id range collides with a non-dummy account';
    END IF;

    SET @dummy_sql = CONCAT(
        'SELECT ''Item'' AS table_name, COUNT(*) AS delete_count FROM `', REPLACE(v_game_db_name, '`', '``'), '`.`Item` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'SELECT ''Currency'' AS table_name, COUNT(*) AS delete_count FROM `', REPLACE(v_game_db_name, '`', '``'), '`.`Currency` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'SELECT ''Characters'' AS table_name, COUNT(*) AS delete_count FROM `', REPLACE(v_game_db_name, '`', '``'), '`.`Characters` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'SELECT ''AccountCurrency'' AS table_name, COUNT(*) AS delete_count FROM `', REPLACE(v_game_db_name, '`', '``'), '`.`AccountCurrency` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SELECT account_id, login_name, game_db_index
      FROM tmp_dummy_accounts
     ORDER BY account_id;

    SELECT p_apply AS apply_changes,
           p_prefix AS login_prefix,
           p_count AS create_count,
           p_account_id_base + 1 AS first_account_id,
           p_account_id_base + p_count AS last_account_id,
           p_target_game_db_index AS target_game_db_index,
           v_game_db_name AS target_game_db_name;

    IF NOT p_apply THEN
        DROP TEMPORARY TABLE tmp_dummy_accounts;
        LEAVE main;
    END IF;

    START TRANSACTION;

    SET @dummy_sql = CONCAT(
        'DELETE t FROM `', REPLACE(v_game_db_name, '`', '``'),
        '`.`Item` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'DELETE t FROM `', REPLACE(v_game_db_name, '`', '``'),
        '`.`Currency` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'DELETE t FROM `', REPLACE(v_game_db_name, '`', '``'),
        '`.`Characters` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    SET @dummy_sql = CONCAT(
        'DELETE t FROM `', REPLACE(v_game_db_name, '`', '``'),
        '`.`AccountCurrency` t JOIN tmp_dummy_accounts d ON d.account_id = t.account_id'
    );
    PREPARE dummy_stmt FROM @dummy_sql;
    EXECUTE dummy_stmt;
    DEALLOCATE PREPARE dummy_stmt;

    DELETE a
      FROM Accounts a
      JOIN tmp_dummy_accounts d ON d.account_id = a.account_id;

    WHILE v_index <= p_count DO
        SET v_account_id = p_account_id_base + v_index;
        SET v_login_name = CONCAT(p_prefix, v_index);

        INSERT INTO Accounts (account_id, login_name, data)
        VALUES (
            v_account_id,
            v_login_name,
            JSON_OBJECT(
                'account_id', CAST(v_account_id AS CHAR),
                'login_name', v_login_name,
                'login_password_hash', v_login_name,
                'game_db_index', p_target_game_db_index
            )
        );

        SET v_index = v_index + 1;
    END WHILE;

    UPDATE GameDBRegistry r
       SET account_count = (
           SELECT COUNT(*)
             FROM Accounts a
            WHERE COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(a.data, '$.game_db_index')) AS SIGNED), 0) = r.game_db_index
       )
     WHERE r.game_db_index = p_target_game_db_index;

    COMMIT;

    DROP TEMPORARY TABLE tmp_dummy_accounts;

    SELECT account_id, login_name,
           JSON_UNQUOTE(JSON_EXTRACT(data, '$.game_db_index')) AS game_db_index
      FROM Accounts
     WHERE login_name REGEXP CONCAT('^', p_prefix, '[0-9]+$')
     ORDER BY account_id;
END//

DELIMITER ;

-- 기본: 미리보기만 수행한다.
-- 실제 적용: FALSE를 TRUE로 변경한다.
CALL ResetDummyAccounts(FALSE, 'dummy', 20, 11000000, 0);

DROP PROCEDURE ResetDummyAccounts;
