"""AI Passport 静默后台守护服务 (Silent Bridge Daemon)。

专为免 CMD 窗口设计：
- 兼容 pythonw.exe 运行模式（自动重定向 stdout/stderr 至日志文件，彻底杜绝 NoneType.write 崩溃）；
- 单实例进程锁，防止多开冲突；
- 具备断线自动秒级重连机制；
- 详细运行日志自动写入 logs/passport_service.log。
"""

from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
LOG_DIR = ROOT / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)
LOG_FILE = LOG_DIR / "passport_service.log"
PID_FILE = LOG_DIR / "passport_service.pid"

# 立即写入启动标记
try:
    with open(str(LOG_FILE), "a", encoding="utf-8") as _f:
        _f.write(f"\n[{time.strftime('%Y-%m-%d %H:%M:%S')}] Python 进程进入 silent_bridge.py (PID: {os.getpid()})\n")
except Exception:
    pass

# 关键保障：无条件重定向标准输出与错误输出至日志文件，杜绝 pythonw 无控制台崩溃
log_stream = open(str(LOG_FILE), "a", encoding="utf-8", buffering=1)
sys.stdout = log_stream
sys.stderr = log_stream

# 配置 logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(log_stream),
    ],
)
logger = logging.getLogger("passport_silent")


def check_and_set_pid() -> bool:
    """确保单实例运行，避免多进程抢占蓝牙。"""
    if PID_FILE.exists():
        try:
            old_pid = int(PID_FILE.read_text(encoding="utf-8").strip())
            import psutil
            if psutil.pid_exists(old_pid):
                try:
                    p = psutil.Process(old_pid)
                    cmd = " ".join(p.cmdline()).lower()
                    if "silent_bridge" in cmd and p.pid != os.getpid():
                        logger.warning(f"AI Passport 服务已在运行 (PID: {old_pid})，本实例退出。")
                        return False
                except Exception:
                    pass
        except Exception:
            pass

    PID_FILE.write_text(str(os.getpid()), encoding="utf-8")
    return True


def handle_uncaught_exception(exc_type, exc_value, exc_traceback):
    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
        return
    logger.critical("未捕获的致命异常导致服务崩溃:", exc_info=(exc_type, exc_value, exc_traceback))


sys.excepthook = handle_uncaught_exception


def remove_pid() -> None:
    try:
        if PID_FILE.exists():
            PID_FILE.unlink()
    except Exception:
        pass


async def main() -> None:
    if not check_and_set_pid():
        sys.exit(0)

    print("\n" + "=" * 52, flush=True)
    print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] AI Passport 静默后台守护服务启动 (PID: {os.getpid()})", flush=True)
    print("=" * 52, flush=True)

    # 导入桥接主逻辑
    sys.path.insert(0, str(ROOT / "tools"))
    from token_monitor_bridge import run_bridge

    try:
        await run_bridge(device_name=None, dry_run=False)
    except Exception as exc:
        print(f"[ERR] 后台守护进程异常: {exc}", flush=True)
        logger.error("后台守护进程异常: %s", exc, exc_info=True)
    finally:
        remove_pid()
        print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] AI Passport 静默后台服务已退出。", flush=True)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        remove_pid()
