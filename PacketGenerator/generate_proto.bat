@echo off
setlocal

set PROTO_ROOT=%~dp0Proto
set CPP_OUT=%~dp0Generated
set CS_OUT=%~dp0..\..\Client\Assets\Generated

set PROTOC=%~dp0..\vcpkg\x64-windows-static-md-2022\tools\protobuf\protoc.exe

echo [PacketGenerator] Creating output directories...
if not exist "%CPP_OUT%"                  mkdir "%CPP_OUT%"
if not exist "%CPP_OUT%\Common"           mkdir "%CPP_OUT%\Common"
if not exist "%CPP_OUT%\GamePacket"       mkdir "%CPP_OUT%\GamePacket"
if not exist "%CPP_OUT%\ServerPacket"     mkdir "%CPP_OUT%\ServerPacket"
if not exist "%CPP_OUT%\DataStructures"   mkdir "%CPP_OUT%\DataStructures"
if not exist "%CS_OUT%"                   mkdir "%CS_OUT%"
if not exist "%CS_OUT%\Common"            mkdir "%CS_OUT%\Common"
if not exist "%CS_OUT%\GamePacket"        mkdir "%CS_OUT%\GamePacket"
if not exist "%CS_OUT%\DataStructures"    mkdir "%CS_OUT%\DataStructures"


echo [PacketGenerator] Running protoc...

:: Common - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%CPP_OUT%" --csharp_out="%CS_OUT%\Common" "%PROTO_ROOT%\Common\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] Common & exit /b 1 )

:: GamePacket - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%CPP_OUT%" --csharp_out="%CS_OUT%\GamePacket" "%PROTO_ROOT%\GamePacket\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] GamePacket & exit /b 1 )

:: ServerPacket - C++ only
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%CPP_OUT%" "%PROTO_ROOT%\ServerPacket\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] ServerPacket & exit /b 1 )

:: DataStructures - C++ / C#
%PROTOC% --proto_path="%PROTO_ROOT%" --cpp_out="%CPP_OUT%" --csharp_out="%CS_OUT%\DataStructures" "%PROTO_ROOT%\DataStructures\*.proto"
if %errorlevel% neq 0 ( echo [ERROR] DataStructures & exit /b 1 )

echo [PacketGenerator] Done.
endlocal
