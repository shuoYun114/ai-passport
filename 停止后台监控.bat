@echo off
title 停止 AI Passport 后台服务

echo ========================================================
echo        正在停止 AI Passport 后台监控服务...
echo ========================================================

python "tools\stop_service.py"

echo.
pause
