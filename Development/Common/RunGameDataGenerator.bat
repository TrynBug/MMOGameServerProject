@echo off
chcp 65001 > nul
setlocal

:: 주의: 이 파일은 파일이 처음 위치해있던 경로에서 실행해야 합니다. 다른 경로들이 여기에 맞춰져있기 때문입니다.

set TOOL=%~dp0..\Server\OUTPUT\Tools\Debug\net8.0\GameDataGenerator.exe

set ENUM_XLSX=%~dp0GameData\Enum\GameEnum.xlsx
set DATA_DIR=%~dp0GameData\

set SERVER_CODE=%~dp0..\Server\GameDataLib\Generated
set SERVER_ENUM=%~dp0..\Server\GameDataLib\Generated\Enum
set SERVER_CSV=%~dp0..\Server\OUTPUT\GameData

set CLIENT_CODE=%~dp0..\Client\Assets\GameData\Generated
set CLIENT_ENUM=%~dp0..\Client\Assets\GameData\Generated\Enum
set CLIENT_CSV=%~dp0..\Client\Assets\GameData

echo 현재경로 = %~dp0
echo GameDataGenerator 경로 = %TOOL%
echo GameEnum.xlsx 경로 = %ENUM_XLSX%
echo GameData 경로 = %DATA_DIR%
echo Server 코드파일 경로 = %SERVER_CODE%
echo Server enum파일 경로 = %SERVER_ENUM%
echo Server 데이터파일 경로 = %SERVER_CSV%
echo Client 코드파일 경로 = %CLIENT_CODE%
echo Client enum파일 경로 = %CLIENT_ENUM%
echo Client 데이터파일 경로 = %CLIENT_CSV%

:: -------------------------------------------------------
:: 1. enum 생성
:: -------------------------------------------------------
echo [1/3] enum 생성 중...
"%TOOL%" enum "%ENUM_XLSX%" ^
    --server-code "%SERVER_ENUM%" ^
    --client-code "%CLIENT_ENUM%"
if %ERRORLEVEL% neq 0 (
    echo [오류] enum 생성 실패
    pause
    exit /b 1
)

:: -------------------------------------------------------
:: 2. 데이터 파일 생성 (DATA_DIR 안의 모든 .xlsx 처리)
:: -------------------------------------------------------
echo [2/3] 데이터 생성 중...
for %%f in (%DATA_DIR%*.xlsx) do (
    echo   처리: %%~nxf
    "%TOOL%" data "%%f" --enum-xlsx "%ENUM_XLSX%" --server-code "%SERVER_CODE%" --server-csv "%SERVER_CSV%" --client-code "%CLIENT_CODE%" --client-csv "%CLIENT_CSV%" --mode all
    if %ERRORLEVEL% neq 0 (
        echo [오류] 데이터 생성 실패: %%~nxf
        pause
        exit /b 1
    )
)

:: -------------------------------------------------------
:: 3. Manager 파일 생성 (Generated 폴더 스캔 후 createAllGameDataTables 생성)
:: -------------------------------------------------------
echo [3/3] Manager 파일 생성 중...
"%TOOL%" merge --server-code "%SERVER_CODE%" --client-code "%CLIENT_CODE%"
if %ERRORLEVEL% neq 0 (
    echo [오류] Manager 파일 생성 실패
    pause
    exit /b 1
)

echo.
echo 완료.
pause
