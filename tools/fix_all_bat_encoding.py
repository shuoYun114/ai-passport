from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BAT_FILES = {
    "一键烧录全新固件.bat": """@echo off
title AI Passport 极客固件一键烧录

echo ========================================================
echo        FoloToy AI Passport 全新极客固件一键烧录
echo ========================================================
echo.
echo [*] 请使用 USB Type-C 数据线将 AI Passport 连接到电脑。
echo [*] 程序将自动识别设备串口 (COM3 等) 并烧录全新固件。
echo.
pause

python "tools\\flash_firmware.py"

echo.
pause
""",
    "停止后台监控.bat": """@echo off
title 停止 AI Passport 后台服务

echo ========================================================
echo        正在停止 AI Passport 后台监控服务...
echo ========================================================

python "tools\\stop_service.py"

echo.
pause
""",
    "设置开机自动启动.bat": """@echo off
title 设置 AI Passport 开机自启

echo ========================================================
echo        正在将 AI Passport 静默服务加入 Windows 开机自启...
echo ========================================================

python "tools\\setup_autostart.py"

echo.
pause
""",
    "取消开机自动启动.bat": """@echo off
title 取消 AI Passport 开机自启

echo ========================================================
echo        正在移除 AI Passport 开机自启项...
echo ========================================================

python "tools\\remove_autostart.py"

echo.
pause
""",
    "查看实时日志.bat": """@echo off
title AI Passport 后台运行日志查看器

echo ========================================================
echo        AI Passport 后台运行日志实时监控
echo        (按 Ctrl+C 可随时退出本窗口，不影响后台运行)
echo ========================================================
echo.

if not exist logs\\passport_service.log (
    echo [i] 尚未产生运行日志。请先双击「启动后台监控(免CMD黑框).vbs」启动服务。
    pause
    exit /b
)

powershell -Command "Get-Content -Path 'logs\\passport_service.log' -Wait -Tail 20"
""",
    "启动连接.bat": """@echo off
cd /d "%~dp0"
title AI Passport 终端调试连接器

echo ========================================================
echo        AI Passport 极客桌面副屏 - 终端调试模式
echo ========================================================
echo.
echo [*] 提示：若希望在后台静默运行（无任何 CMD 命令行黑框），
echo     请直接双击运行「启动后台监控(免CMD黑框).vbs」！
echo.
echo [*] 当前正在前台终端启动连接与配对...
echo ========================================================
echo.

python "tools\\token_monitor_bridge.py"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================================
    echo 连接进程异常退出 (错误码: %ERRORLEVEL%)
    echo ========================================================
    pause
)
"""
}

for filename, content in BAT_FILES.items():
    p = ROOT / filename
    normalized = content.replace("\r\n", "\n").replace("\n", "\r\n")
    p.write_bytes(normalized.encode("gbk"))
    print(f"[OK] 已标准 ANSI/GBK 格式写入: {filename}")
