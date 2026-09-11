@echo off
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
content = f'''Set ws = CreateObject("WScript.Shell")
ws.Run """{vbs_path}""", 0, False
'''
shortcut_vbs.write_text(content, encoding='utf-8')
print(f'[OK] 已成功在开机自启目录创建快捷启动项:')
print(f'     {shortcut_vbs}')
print('
[成功] 以后电脑开机，AI Passport 将在后台自动静默连接，无需手动启动！')
"

echo.
pause
