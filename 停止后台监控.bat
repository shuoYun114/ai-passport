@echo off
chcp 65001 >nul
title 停止 AI Passport 后台监控服务

echo ========================================================
echo         正在停止 AI Passport 后台监控服务...
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

# 辅助检查：清理残留的 silent_bridge
for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
    try:
        cmdline = ' '.join(proc.info['cmdline'] or [])
        if 'silent_bridge.py' in cmdline:
            proc.terminate()
            print(f'[OK] 已清理残留的守护进程 (PID: {proc.info[\"pid\"]})')
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
