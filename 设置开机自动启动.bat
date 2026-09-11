@echo off
title 设置 AI Passport 开机自启

echo ========================================================
echo        正在将 AI Passport 静默服务加入 Windows 开机自启...
echo ========================================================

python "tools\setup_autostart.py"

echo.
pause
