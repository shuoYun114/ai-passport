import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 智能探测 python.exe 的准确位置 (python.exe 具备完整 Console COM 栈，WinRT 蓝牙 100% 稳定，结合 SW_HIDE=0 可实现完美的免黑框后台运行)
candidates = [
    r"C:\Users\Admin\AppData\Local\Python\pythoncore-3.14-64\python.exe",
    sys.executable,
    shutil.which("python"),
    r"C:\Users\Admin\AppData\Local\Python\bin\python.exe",
]

py_path = None
for c in candidates:
    if c and Path(c).exists():
        py_path = str(Path(c).resolve())
        break

if not py_path:
    py_path = "python.exe"

script_path = str((ROOT / "tools" / "silent_bridge.py").resolve())
root_dir = str(ROOT.resolve())

# 1. 生成完全无黑框的 .vbs 脚本 (以纯 ASCII 保存，使用 0 参数隐藏窗口)
vbs_content = f'''Set ws = CreateObject("WScript.Shell")
ws.CurrentDirectory = "{root_dir}"
cmd = Chr(34) & "{py_path}" & Chr(34) & " tools\\silent_bridge.py"
ws.Run cmd, 0, False
Set ws = Nothing
'''

vbs_file = ROOT / "启动后台监控(免CMD黑框).vbs"
vbs_file.write_bytes(vbs_content.replace("\r\n", "\n").replace("\n", "\r\n").encode("gbk"))
print(f"[OK] 已生成免黑框 VBS 启动脚本: {vbs_file.name}")

# 2. 生成一键批处理 (双击后秒级拉起 VBS 并自动关闭 CMD 窗口)
bat_content = f'''@echo off
cd /d "%~dp0"
wscript //nologo "%~dp0启动后台监控(免CMD黑框).vbs"
exit
'''
bat_file = ROOT / "启动后台监控.bat"
bat_file.write_bytes(bat_content.replace("\r\n", "\n").replace("\n", "\r\n").encode("gbk"))
print(f"[OK] 已生成双保险批处理启动脚本: {bat_file.name}")
