@echo off

set "ILLIXR_INSTALL="
set "ILLIXR_BUILD=\out\build\x64-Release"

set "PATH=%ILLIXR_INSTALL%\lib;%ILLIXR_BUILD%\vcpkg_installed\x64-windows\bin;%ILLIXR_BUILD%\vcpkg_installed\x64-windows\debug\bin;%PATH%"

set "ILLIXR_PATH=%ILLIXR_INSTALL%\lib\plugin.main.opt.dll"
set "ILLIXR_COMP=%ILLIXR_INSTALL%\lib\plugin.tcp_network_backend.dbg.dll;%ILLIXR_INSTALL%\lib\plugin.udp_network_backend.dbg.dll;%ILLIXR_INSTALL%\lib\plugin.network_latency.rx.dbg.dll;%ILLIXR_INSTALL%\lib\plugin.offload_rendering_server.dbg.dll"
set "XR_RUNTIME_JSON=%ILLIXR_INSTALL%\openxr_monado_vk.json"

set "ILLIXR_TCP_CLIENT_PORT=9000"
set "ILLIXR_TCP_CLIENT_IP=192.168.8.140"
set "ILLIXR_TCP_SERVER_IP=192.168.8.203"
set "ILLIXR_TCP_SERVER_PORT=9001"

set "ILLIXR_SERVER_HEIGHT=1816"
set "ILLIXR_SERVER_WIDTH=1680"
set "ILLIXR_IS_CLIENT=0"
set "ILLIXR_DISPLAY_MODE=none"
set "ILLIXR_RUN_DURATION=10000"
set "ILLIXR_OFFLOAD_RENDERING_FRAMERATE=90"
set "ILLIXR_USE_HAND_TRACKING=1"
set "ILLIXR_USE_DEPTH_IMAGES=0"
set "ILLIXR_USE_MOTION_VECTOR_IMAGES=0"
set "ILLIXR_USE_PALM_POSES=1"
set "ILLIXR_USE_HAND_INTERACTIONS=1"
set "ILLIXR_OFFLOAD_RENDERING_BITRATE=100000000"
set "ILLIXR_ENABLE_POSE_PREDICTION=1"
set "ILLIXR_OVERSCAN=1.1"

"%ILLIXR_INSTALL%\bin\monado-service.exe"
