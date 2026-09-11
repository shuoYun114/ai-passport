@echo off
title AI Passport 极客固件一键烧录

echo ========================================================
echo        FoloToy AI Passport 全新极客固件一键烧录
echo ========================================================
echo.
echo [*] 请使用 USB Type-C 数据线将 AI Passport 连接到电脑。
echo [*] 程序将自动识别设备串口 (COM3 等) 并烧录全新固件。
echo.
pause

python "tools\flash_firmware.py"

echo.
pause
