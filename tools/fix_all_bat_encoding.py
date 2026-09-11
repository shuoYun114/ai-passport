"""重写并以标准 ANSI/GBK 编码保存所有 Windows 批处理脚本。

彻底杜绝 Windows CMD 因 UTF-8 跨行截断导致的
"'xxxx' 不是内部或外部命令，也不是可运行的程序或批处理文件" 报错。
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BAT_FILES = {
    "一键烧录全新固件.bat": """@echo off
rem ========================================================
rem        FoloToy AI Passport 全新极客固件一键烧录
rem ========================================================
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
rem ========================================================
rem        停止 AI Passport 后台监控服务
rem ========================================================
title 停止 AI Passport 后台服务

echo ========================================================
echo        正在停止 AI Passport 后台监控服务...
echo ========================================================

python -c "
import os, psutil
from pathlib import Path

pid_file = Path('logs/passport_service.pid')
stopped = False
if pid_file.exists():
    try:
        pid = int(pid_file.read_text(encoding='utf-8').strip())
        if psutil.pid_exists(pid):
            p = psutil.Process(pid)
            p.terminate()
            p.wait(timeout=3)
            print(f'[OK] 已成功终止后台服务进程 (PID: {pid})')
            stopped = True
    except Exception as e:
        print(f'[!] 终止 PID 出错: {e}')
    finally:
        try:
            pid_file.unlink()
        except Exception:
            pass

for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
    try:
        cmdline = ' '.join(proc.info['cmdline'] or [])
        if 'silent_bridge.py' in cmdline:
            proc.terminate()
            print(f'[OK] 已清理残留守护进程 (PID: {proc.info[\"pid\"]})')
            stopped = True
    except Exception:
        pass

if not stopped:
    print('[i] 当前没有正在运行的 AI Passport 后台服务。')
else:
    print('[OK] 后台服务已彻底停止。')
"

echo.
pause
""",
    "设置开机自动启动.bat": """@echo off
rem ========================================================
rem        设置 AI Passport 开机自启
rem ========================================================
title 设置 AI Passport 开机自启

echo ========================================================
echo        正在将 AI Passport 静默服务加入 Windows 开机自启...
echo ========================================================

python -c "
import os, sys
from pathlib import Path

startup_dir = Path(os.environ['APPDATA']) / 'Microsoft' / 'Windows' / 'Start Menu' / 'Programs' / 'Startup'
vbs_path = Path.cwd() / '启动后台监控(免CMD黑框).vbs'

if not vbs_path.exists():
    print(f'[-] 未找到启动脚本: {vbs_path}')
    sys.exit(1)

shortcut_vbs = startup_dir / 'AI_Passport_Silent_Bridge.vbs'
content = f'''Set ws = CreateObject(\"WScript.Shell\")
ws.Run \"\"\"{vbs_path}\"\"\", 0, False
'''
shortcut_vbs.write_text(content, encoding='utf-8')
print(f'[OK] 已成功在开机自启目录创建快捷启动项:')
print(f'     {shortcut_vbs}')
print('\n[成功] 以后电脑开机，AI Passport 将在后台自动静默连接，无需手动启动！')
"

echo.
pause
""",
    "取消开机自动启动.bat": """@echo off
rem ========================================================
rem        取消 AI Passport 开机自启
rem ========================================================
title 取消 AI Passport 开机自启

echo ========================================================
echo        正在移除 AI Passport 开机自启项...
echo ========================================================

python -c "
import os
from pathlib import Path

startup_dir = Path(os.environ['APPDATA']) / 'Microsoft' / 'Windows' / 'Start Menu' / 'Programs' / 'Startup'
shortcut_vbs = startup_dir / 'AI_Passport_Silent_Bridge.vbs'

if shortcut_vbs.exists():
    try:
        shortcut_vbs.unlink()
        print(f'[OK] 已成功移除开机自启文件: {shortcut_vbs}')
    except Exception as e:
        print(f'[-] 移除失败: {e}')
else:
    print('[i] 开机自启目录中未发现 AI Passport 启动项，无需清理。')
"

echo.
pause
""",
    "查看实时日志.bat": """@echo off
rem ========================================================
rem        查看 AI Passport 后台运行日志
rem ========================================================
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
rem ========================================================
rem        AI Passport 极客桌面副屏 - 终端调试模式
rem ========================================================
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
    # 使用 CRLF 换行符与标准 GBK 编码写入
    normalized = content.replace("\r\n", "\n").replace("\n", "\r\n")
    p.write_bytes(normalized.encode("gbk"))
    print(f"[OK] 已标准 ANSI/GBK 编码重写保存: {filename}")
