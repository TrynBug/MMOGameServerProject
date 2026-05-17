@echo off
REM ============================================================
REM GameDB 초기화 배치파일
REM ============================================================
REM DBSchema.sql과 DBDataSample.sql을 OUTPUT/Debug/GameDB.db에 적용한다.
REM
REM 전제: sqlite3.exe가 PATH에 있어야 함.
REM        없다면 https://www.sqlite.org/download.html 에서 다운로드 후 PATH 설정.
REM
REM 사용:
REM   1) 이 배치파일을 더블클릭하거나 명령 프롬프트에서 실행
REM   2) 기존 GameDB.db에 IF NOT EXISTS / OR IGNORE 로 안전하게 적용됨
REM ============================================================

setlocal
set "DB_DIR=%~dp0..\BIN\Debug"
set "DB_PATH=%DB_DIR%\GameDB.db"
set "SCHEMA_SQL=%~dp0DBSchema.sql"
set "SAMPLE_SQL=%~dp0DBDataSample.sql"

REM 대상 폴더가 없으면 생성
if not exist "%DB_DIR%" mkdir "%DB_DIR%"

echo.
echo [1/2] Applying schema to %DB_PATH%
sqlite3 "%DB_PATH%" < "%SCHEMA_SQL%"
if errorlevel 1 (
    echo [ERROR] Failed to apply schema. Make sure sqlite3.exe is in PATH.
    exit /b 1
)

echo [2/2] Inserting sample data
sqlite3 "%DB_PATH%" < "%SAMPLE_SQL%"
if errorlevel 1 (
    echo [ERROR] Failed to insert sample data.
    exit /b 1
)

echo.
echo Done. GameDB.db is ready at %DB_PATH%
endlocal
