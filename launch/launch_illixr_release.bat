@echo off

set "ILLIXR_INSTALL="
set "ILLIXR_BUILD=/out/build/x64-Release"

set "PATH=%ILLIXR_INSTALL%/lib;%ILLIXR_INSTALL%/bin;%PATH%"

set "ILLIXR_TCP_CLIENT_PORT=9000"
set "ILLIXR_UDP_CLIENT_PORT=9002"
set "ILLIXR_TCP_CLIENT_IP="
set "ILLIXR_TCP_SERVER_IP="
set "ILLIXR_TCP_SERVER_PORT=9001"
set "ILLIXR_UDP_SERVER_PORT=9003"

set "ILLIXR_IS_CLIENT=0"
set "ILLIXR_DISPLAY_MODE=none"
set "ILLIXR_RUN_DURATION=10000"

"%ILLIXR_INSTALL%/bin/monado-service.exe -y profiles/unity_server.yaml"
