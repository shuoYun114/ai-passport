"""AI Passport 静默后台守护服务 (Silent Bridge Daemon)。

专为免 CMD 窗口设计：
- 通过 pythonw.exe 运行，无任何控制台黑框弹出；
- 单实例进程锁，防止多开冲突；
- 具备断线自动秒级重连机制；
- 详细运行日志自动写入 logs/passport_service.log，便于排查状态。
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

# 配置日志输出到文件
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(str(LOG_FILE), encoding="utf-8", mode="a"),
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
                    if "python" in p.name().lower():
                        logger.warning(f"服务已在运行 (PID: {old_pid})，本实例退出。")
                        return False
                except Exception:
                    pass
        except Exception:
            pass

    PID_FILE.write_text(str(os.getpid()), encoding="utf-8")
    return True


def remove_pid() -> None:
    try:
        if PID_FILE.exists():
            PID_FILE.unlink()
    except Exception:
        pass


async def main() -> None:
    if not check_and_set_pid():
        sys.exit(0)

    logger.info("==========================================")
    logger.info("AI Passport 静默后台守护服务已启动 (PID: %d)", os.getpid())
    logger.info("==========================================")

    # 导入桥接主逻辑
    sys.path.insert(0, str(ROOT / "tools"))
    from token_monitor_bridge import run_bridge

    try:
        await run_bridge(device_name=None, dry_run=False)
    except Exception as exc:
        logger.error("后台守护进程发生异常: %s", exc, exc_info=True)
    finally:
        remove_pid()
        logger.info("AI Passport 静默后台守护服务已终止。")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        remove_pid()
