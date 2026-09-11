import os
from pathlib import Path
import psutil

ROOT = Path(__file__).resolve().parent.parent
pid_file = ROOT / "logs" / "passport_service.pid"
stopped = False

# 1. 尝试通过 PID 文件停止
if pid_file.exists():
    try:
        pid = int(pid_file.read_text(encoding="utf-8").strip())
        if psutil.pid_exists(pid):
            p = psutil.Process(pid)
            p.terminate()
            p.wait(timeout=3)
            print(f"[OK] 已成功终止后台服务进程 (PID: {pid})")
            stopped = True
    except Exception as e:
        print(f"[!] 终止 PID 出错: {e}")
    finally:
        try:
            pid_file.unlink()
        except Exception:
            pass

# 2. 扫描清理所有残留的 silent_bridge.py 或 token_monitor_bridge.py
for proc in psutil.process_iter(["pid", "name", "cmdline"]):
    try:
        cmdline = " ".join(proc.info["cmdline"] or [])
        if "silent_bridge.py" in cmdline or "token_monitor_bridge.py" in cmdline:
            proc.terminate()
            print(f"[OK] 已清理残留守护进程 (PID: {proc.info['pid']})")
            stopped = True
    except Exception:
        pass

if not stopped:
    print("[i] 当前没有正在运行的 AI Passport 后台服务。")
else:
    print("[OK] 后台服务已彻底停止。")
