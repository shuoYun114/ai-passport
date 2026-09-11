@echo off
rem ========================================================
rem        查看 AI Passport 后台运行日志
rem ========================================================
title AI Passport 后台运行日志查看器

echo ========================================================
echo        AI Passport 后台运行日志实时监控
echo        (按 Ctrl+C 可随时退出本窗口，不影响后台运行)
echo ========================================================
echo.

if not exist logs\passport_service.log (
    echo [i] 尚未产生运行日志。请先双击「启动后台监控(免CMD黑框).vbs」启动服务。
    pause
    exit /b
)

powershell -Command "Get-Content -Path 'logs\passport_service.log' -Wait -Tail 20"
