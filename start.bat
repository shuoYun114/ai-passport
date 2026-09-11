@echo off
cd /d "%~dp0"
title AI Passport Token Monitor
python "tools\token_monitor_bridge.py"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================================
    echo Process exited with error code %ERRORLEVEL%
    echo ========================================================
    pause
)
