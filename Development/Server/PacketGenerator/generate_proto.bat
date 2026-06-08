@echo off
setlocal

set PROTO_ROOT=%~dp0Proto
set CPP_OUT=%~dp0Generated
set CS_OUT=%~dp0..\..\Client\Assets\Generated

set PROTOC=%~dp0..\vcpkg\x64-windows-static-md-2022\tools\protobuf\protoc.exe

set TMP_ROOT=%~dp0__protoc_tmp
set TMP_CPP=%TMP_ROOT%\cpp
set TMP_CS=%TMP_ROOT%\cs

echo [PacketGenerator] Preparing temp staging...
if exist "%TMP_ROOT%" rmdir /s /q "%TMP_ROOT%"
mkdir "%TMP_CPP%\Common"
mkdir "%TMP_CPP%\GamePacket"
mkdir "%TMP_CPP%\ServerPacket"
mkdir "%TMP_CPP%\DataStructures"
mkdir "%TMP_CS%\Common"
mkdir "%TMP_CS%\GamePacket"
mkdir "%TMP_CS%\DataStructures"


:: ----------------------------------------------------------------
:: 먼저 임시폴더에 proto message 파일을 생성한다. 
:: 그런다음 아래에서 임시폴더 파일의 내용과 기존폴더 파일의 내용을 비교하여 변경된 것만 교체한다.
:: ----------------------------------------------------------------
echo [PacketGenerator] Running protoc...

:: Common - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%TMP_CPP%" --csharp_out="%TMP_CS%\Common" "%PROTO_ROOT%\Common\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] Common & rmdir /s /q "%TMP_ROOT%" & exit /b 1 )

:: GamePacket - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%TMP_CPP%" --csharp_out="%TMP_CS%\GamePacket" "%PROTO_ROOT%\GamePacket\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] GamePacket & rmdir /s /q "%TMP_ROOT%" & exit /b 1 )

:: ServerPacket - C++ only
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%TMP_CPP%" "%PROTO_ROOT%\ServerPacket\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] ServerPacket & rmdir /s /q "%TMP_ROOT%" & exit /b 1 )

:: DataStructures - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%TMP_CPP%" --csharp_out="%TMP_CS%\DataStructures" "%PROTO_ROOT%\DataStructures\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] DataStructures & rmdir /s /q "%TMP_ROOT%" & exit /b 1 )

echo [PacketGenerator] Syncing changed files...
if not exist "%CPP_OUT%" mkdir "%CPP_OUT%"
if not exist "%CS_OUT%"  mkdir "%CS_OUT%"
call :sync "%TMP_CPP%" "%CPP_OUT%"
call :sync "%TMP_CS%"  "%CS_OUT%"

rmdir /s /q "%TMP_ROOT%"
echo [PacketGenerator] Done.
endlocal
exit /b 0

:: ----------------------------------------------------------------
:: 임시 폴더(%~1)의 파일을 대상 폴더(%~2)와 내용(해시) 비교하여
:: 다르거나 새로 생긴 파일만 복사한다. 동일한 파일은 건드리지 않아
:: 타임스탬프가 유지되므로 불필요한 재컴파일을 막는다.
:: ----------------------------------------------------------------

:: 아래 PowerShell 한 줄은 다음 순서로 동작한다:
::  1) $src = 원본(임시) 폴더, $dst = 대상(실제 출력) 폴더 경로를 변수에 저장
::  2) Get-ChildItem -LiteralPath $src -Recurse -File
::     -> 임시 폴더 하위의 모든 파일을 재귀적으로 나열
::  3) ForEach-Object { ... } : 나열된 각 파일에 대해 아래를 반복
::  4) $rel = 임시 폴더 기준 상대경로 추출 (예: Common\Foo.pb.h)
::     ($_.FullName 에서 $src 길이만큼 잘라내고 앞의 \ 를 제거)
::  5) $t   = Join-Path $dst $rel : 대상 폴더에서의 대응 경로
::  6) $copy = $true (기본값: 복사함)
::     대상에 같은 경로 파일이 이미 있고, 두 파일의 해시(Get-FileHash)가
::     같으면 내용이 동일하므로 $copy = $false (복사 건너뜀 -> 타임스탬프 유지)
::  7) $copy 가 true 이면 (파일이 없거나 내용이 다름):
::     - Split-Path 로 대상 폴더 경로를 구하고, 없으면 New-Item 으로 생성
::     - Copy-Item -Force 로 파일 복사(덮어쓰기)
::     - Write-Host 로 '  updated: 상대경로' 출력하여 교체된 파일을 표시
:sync
powershell -NoProfile -ExecutionPolicy Bypass -Command "$src='%~1'; $dst='%~2'; Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object { $rel = $_.FullName.Substring($src.Length).TrimStart('\'); $t = Join-Path $dst $rel; $copy = $true; if (Test-Path -LiteralPath $t) { if ((Get-FileHash -LiteralPath $_.FullName).Hash -eq (Get-FileHash -LiteralPath $t).Hash) { $copy = $false } }; if ($copy) { $d = Split-Path -Parent $t; if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }; Copy-Item -LiteralPath $_.FullName -Destination $t -Force; Write-Host ('  updated: ' + $rel) } }"

pause
exit /b 0
