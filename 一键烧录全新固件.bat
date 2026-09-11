@echo off
chcp 65001 >nul
title AI Passport 一键固件烧录

echo ========================================================
echo        FoloToy AI Passport 全新极客固件一键烧录
echo ========================================================
echo.
echo [*] 请使用 USB Type-C 数据线将 AI Passport 连接到电脑 USB 口。
echo [*] 程序将自动识别设备串口并烧录最终高质感固件。
echo.
pause

python "tools\flash_firmware.py"

echo.
if %ERRORLEVEL% EQU 0 (
    echo [OK] 烧录成功！设备已自动重启，全新极客界面已就绪。
) else (
    echo [!] 烧录未完成，请检查 USB 线缆是否连接牢固或是否有其他程序占用串口。
)
echo.
pause
