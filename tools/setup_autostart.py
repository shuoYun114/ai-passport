import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
startup_dir = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"
vbs_path = ROOT / "启动后台监控(免CMD黑框).vbs"

if not vbs_path.exists():
    print(f"[-] 未找到启动脚本: {vbs_path}")
    sys.exit(1)

shortcut_vbs = startup_dir / "AI_Passport_Silent_Bridge.vbs"
content = f'''Set ws = CreateObject("WScript.Shell")
ws.Run """{vbs_path}""", 0, False
'''
shortcut_vbs.write_text(content, encoding="utf-8")
print("[OK] 已成功在 Windows 开机自启目录注册快捷启动项:")
print(f"     {shortcut_vbs}")
print("\n[成功] 以后每次电脑开机，AI Passport 将在后台自动静默同步，桌面无任何黑框！")
