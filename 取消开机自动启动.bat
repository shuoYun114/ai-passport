@echo off
title 取消 AI Passport 开机自启

echo ========================================================
echo        正在移除 AI Passport 开机自启项...
echo ========================================================

python "tools\remove_autostart.py"

echo.
pause
