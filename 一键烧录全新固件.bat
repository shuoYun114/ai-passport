@echo off
@chcp 65001 >nul
title AI Passport 极低功耗固件一键烧录

echo ========================================================
echo        FoloToy AI Passport 极低功耗精简固件一键烧录
echo ========================================================
echo.
echo [*] 请使用 USB Type-C 数据线将 AI Passport 连接到电脑。
echo [*] 脚本将智能识别设备串口并自动烧录最新低功耗固件。
echo.

python "tools\flash_firmware.py"

echo.
pause
