import os
from pathlib import Path

startup_dir = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"
shortcut_vbs = startup_dir / "AI_Passport_Silent_Bridge.vbs"

if shortcut_vbs.exists():
    try:
        shortcut_vbs.unlink()
        print(f"[OK] 已成功从开机自启目录移除: {shortcut_vbs.name}")
    except Exception as e:
        print(f"[-] 移除失败: {e}")
else:
    print("[i] 开机自启目录中未发现 AI Passport 启动项，无需清理。")
