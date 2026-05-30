@echo off
chcp 65001 > nul
setlocal

:: 주의: 이 파일은 파일이 처음 위치해있던 경로에서 실행해야 합니다. 다른 경로들이 여기에 맞춰져있기 때문입니다.
::
:: 사용법:
::   RunGameDataGenerator2.bat enum            : enum 만 생성
::   RunGameDataGenerator2.bat code ^<데이터명^>  : 특정 게임데이터의 코드만 생성       (예: code Monster)
::   RunGameDataGenerator2.bat csv  ^<데이터명^>  : 특정 게임데이터의 데이터(csv)만 생성 (예: csv  Monster)
::   RunGameDataGenerator2.bat merge           : Manager 파일만 생성 (Generated 폴더 스캔)
::   RunGameDataGenerator2.bat all             : 전체 생성 (enum + 모든 데이터 + merge)
::
::   - ^<데이터명^> 은 Common\GameData\ 안의 xlsx 파일명 입니다. 확장자(.xlsx)는 생략 가능합니다.

set TOOL=%~dp0..\Server\OUTPUT\Tools\Debug\net8.0\GameDataGenerator.exe

set ENUM_XLSX=%~dp0GameData\Enum\GameEnum.xlsx
set DATA_DIR=%~dp0GameData\

set SERVER_CODE=%~dp0..\Server\GameDataLib\Generated
set SERVER_ENUM=%~dp0..\Server\GameDataLib\Generated\Enum
set SERVER_CSV=%~dp0..\Server\OUTPUT\GameData

set CLIENT_CODE_BASE=%~dp0..\Client\Assets\GameData\Generated\Base
set CLIENT_CODE_CUSTOM=%~dp0..\Client\Assets\GameData\Custom
set CLIENT_CODE_MANAGER=%~dp0..\Client\Assets\GameData\Generated\Manager
set CLIENT_ENUM=%~dp0..\Client\Assets\GameData\Generated\Enum
set CLIENT_CSV=%~dp0..\Client\Assets\StreamingAssets\GameData

set "CMD=%~1"
set "DATANAME=%~2"

if /i "%CMD%"=="enum"  goto :cmd_enum
if /i "%CMD%"=="code"  goto :cmd_code
if /i "%CMD%"=="csv"   goto :cmd_csv
if /i "%CMD%"=="merge" goto :cmd_merge
if /i "%CMD%"=="all"   goto :cmd_all
goto :usage


:: -------------------------------------------------------
:: enum 만 생성
:: -------------------------------------------------------
:cmd_enum
call :gen_enum
if errorlevel 1 goto :fail
goto :done


:: -------------------------------------------------------
:: 특정 게임데이터 - 코드만 / csv만
:: -------------------------------------------------------
:cmd_code
set "MODE=code"
goto :run_data_file

:cmd_csv
set "MODE=csv"
goto :run_data_file

:run_data_file
if "%DATANAME%"=="" (
    echo [오류] 데이터파일명을 입력하세요. 예: RunGameDataGenerator2.bat %CMD% Monster
    goto :listdata
)
if /i "%~x2"==".xlsx" (set "DATA_FILE=%DATA_DIR%%DATANAME%") else (set "DATA_FILE=%DATA_DIR%%DATANAME%.xlsx")
if not exist "%DATA_FILE%" (
    echo [오류] 데이터파일을 찾을 수 없습니다: %DATA_FILE%
    goto :listdata
)
call :gen_data "%DATA_FILE%" %MODE%
if errorlevel 1 goto :fail
goto :done


:: -------------------------------------------------------
:: Manager 파일만 생성
:: -------------------------------------------------------
:cmd_merge
call :gen_merge
if errorlevel 1 goto :fail
goto :done


:: -------------------------------------------------------
:: 전체 생성 (enum + 모든 데이터 + merge)
:: -------------------------------------------------------
:cmd_all
echo [all] 전체 생성 시작...
call :gen_enum
if errorlevel 1 goto :fail
for %%f in ("%DATA_DIR%*.xlsx") do (
    call :gen_data "%%f" all
    if errorlevel 1 goto :fail
)
call :gen_merge
if errorlevel 1 goto :fail
goto :done


:: -------------------------------------------------------
:: 데이터파일 목록 출력 후 종료
:: -------------------------------------------------------
:listdata
echo.
echo 사용 가능한 데이터파일 목록 (%DATA_DIR%):
for %%f in ("%DATA_DIR%*.xlsx") do echo   %%~nf
pause
exit /b 1


:: -------------------------------------------------------
:: 사용법
:: -------------------------------------------------------
:usage
echo 사용법:
echo   RunGameDataGenerator2.bat enum            : enum 만 생성
echo   RunGameDataGenerator2.bat code ^<데이터명^>  : 특정 게임데이터의 코드만 생성       (예: code Monster)
echo   RunGameDataGenerator2.bat csv  ^<데이터명^>  : 특정 게임데이터의 데이터(csv)만 생성 (예: csv  Monster)
echo   RunGameDataGenerator2.bat merge           : Manager 파일만 생성
echo   RunGameDataGenerator2.bat all             : 전체 생성 (enum + 모든 데이터 + merge)
pause
exit /b 1


:: -------------------------------------------------------
:: 실패 종료
:: -------------------------------------------------------
:fail
echo.
echo [실패] 작업 중 오류가 발생했습니다.
pause
exit /b 1


:: -------------------------------------------------------
:: 정상 종료
:: -------------------------------------------------------
:done
echo.
echo 완료.
pause
exit /b 0


:: =======================================================
:: 서브루틴
:: =======================================================
:gen_enum
echo [enum] 생성 중...
"%TOOL%" enum "%ENUM_XLSX%" --server-code "%SERVER_ENUM%" --client-code "%CLIENT_ENUM%"
exit /b %ERRORLEVEL%

:gen_data
:: %1 = xlsx 전체경로, %2 = mode (code / csv / all)
echo [data] 생성 중 (mode=%~2): %~1
"%TOOL%" data "%~1" --enum-xlsx "%ENUM_XLSX%" --server-code "%SERVER_CODE%" --server-csv "%SERVER_CSV%" --client-code-base "%CLIENT_CODE_BASE%" --client-code-custom "%CLIENT_CODE_CUSTOM%" --client-csv "%CLIENT_CSV%" --mode %~2
exit /b %ERRORLEVEL%

:gen_merge
echo [merge] Manager 파일 생성 중...
"%TOOL%" merge --server-code "%SERVER_CODE%" --client-code-custom "%CLIENT_CODE_CUSTOM%" --client-code-manager "%CLIENT_CODE_MANAGER%"
exit /b %ERRORLEVEL%
