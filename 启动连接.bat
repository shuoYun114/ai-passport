@echo off
chcp 65001 >nul
title AI Passport Token Monitor 同步服务
color 0A

echo ================================================================
echo         FoloToy AI Passport Token Monitor 同步服务
echo ================================================================
echo.
echo  请确认：
echo    1. 电脑已开启蓝牙
echo    2. AI Passport 设备已开机并在电脑附近
echo    3. 首次连接时，设备屏幕若弹出 6 位配对码，请在 Windows 弹窗输入
echo.
echo ================================================================
echo.

python tools/token_monitor_bridge.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ================================================================
    echo  同步服务异常退出，按任意键关闭窗口...
    echo ================================================================
    pause >nul
)
