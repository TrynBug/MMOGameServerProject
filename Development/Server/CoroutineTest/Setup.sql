-- 코루틴 예제 DB 스키마
CREATE DATABASE IF NOT EXISTS CoroutineExample;
USE CoroutineExample;

CREATE TABLE IF NOT EXISTS Players (
    player_id INT PRIMARY KEY,
    gold INT NOT NULL
);
INSERT IGNORE INTO Players VALUES (1, 0), (2, 0), (3, 0);

DROP PROCEDURE IF EXISTS ReadGold;
DROP PROCEDURE IF EXISTS WriteGold;
DROP PROCEDURE IF EXISTS FailExample;
DELIMITER //
CREATE PROCEDURE ReadGold(IN playerId INT)
BEGIN
    SELECT gold FROM Players WHERE player_id = playerId;
END//
CREATE PROCEDURE WriteGold(IN playerId INT, IN newGold INT)
BEGIN
    UPDATE Players SET gold = newGold WHERE player_id = playerId;
END//
CREATE PROCEDURE FailExample()
BEGIN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Intentional coroutine example error';
END//
DELIMITER ;
