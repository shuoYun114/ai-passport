@echo off
chcp 65001 >nul
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
