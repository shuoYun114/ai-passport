"""Antigravity 额度检测工具命令行 CLI 接口。"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from typing import Optional

from . import __version__
from .cache import DEFAULT_CACHE_PATH, load_cached_snapshot, roll_expired_quotas, save_cached_snapshot
from .client import AntigravityClient
from .detector import ServerEndpoint, discover_server_endpoint
from .formatter import format_console_dashboard
from .watcher import AntigravitySessionWatcher


def setup_console_encoding() -> None:
    """尝试将标准输出流设置为 UTF-8 编码，避免在 Windows CMD/PowerShell 下乱码。"""
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
            sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def clear_console() -> None:
    """清屏。"""
    os.system("cls" if sys.platform == "win32" else "clear")


def main() -> int:
    setup_console_encoding()

    parser = argparse.ArgumentParser(
        prog="antigravity-usage",
        description="Google Antigravity 模型额度与用量实时检测工具",
    )
    parser.add_argument(
        "-v", "--version", action="version", version=f"%(prog)s {__version__}"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="以 JSON 格式输出机器可读的配额快照",
    )
    parser.add_argument(
        "-w",
        "--watch",
        nargs="?",
        const=5,
        type=int,
        metavar="SECONDS",
        help="开启持续动态监控（默认每 5 秒刷新一次）",
    )
    parser.add_argument(
        "-f",
        "--filter",
        metavar="KEYWORD",
        help="按关键字筛选模型（例如 flash、pro、claude）",
    )
    parser.add_argument(
        "-m",
        "--model",
        metavar="MODEL_NAME",
        help="仅获取指定模型的简短剩余额度（适合脚本或状态栏嵌入）",
    )
    parser.add_argument(
        "--port",
        type=int,
        help="手动指定 Language Server 端口（跳过自动嗅探）",
    )
    parser.add_argument(
        "--token",
        help="手动指定 CSRF Token（跳过自动提取）",
    )
    parser.add_argument(
        "--no-cache",
        action="store_true",
        help="禁用本地磁盘快照缓存",
    )

    args = parser.parse_args()

    # 初始化客户端
    endpoint: Optional[ServerEndpoint] = None
    if args.port and args.token:
        endpoint = ServerEndpoint(port=args.port, csrf_token=args.token)

    client = AntigravityClient(endpoint=endpoint)
    watcher = AntigravitySessionWatcher()

    def get_snapshot():
        try:
            snap = client.fetch_usage_snapshot()
            if not args.no_cache:
                save_cached_snapshot(snap)
            return snap, True
        except Exception as fetch_err:
            if not args.no_cache:
                cached = load_cached_snapshot()
                if cached:
                    roll_expired_quotas(cached)
                    return cached, False
            raise fetch_err

    # 单次指定模型精简输出模式
    if args.model:
        try:
            snapshot, is_live = get_snapshot()
            target_kw = args.model.lower()
            for m in snapshot.models:
                if target_kw in m.label.lower() or target_kw in m.model_id.lower():
                    print(f"{m.remaining_percent:.1f}%")
                    return 0
            print(f"未找到包含 '{args.model}' 的模型", file=sys.stderr)
            return 1
        except Exception as err:
            print(f"获取配额失败: {err}", file=sys.stderr)
            return 1

    # 动态 Watch 监控模式
    if args.watch is not None:
        interval = max(1, args.watch)
        try:
            while True:
                watcher.poll()
                try:
                    snapshot, is_live = get_snapshot()
                    clear_console()
                    dashboard = format_console_dashboard(
                        snapshot,
                        running_task_count=watcher.running_task_count,
                        filter_keyword=args.filter,
                    )
                    source_tip = "(实时在线)" if is_live else "(离线缓存预估)"
                    print(f"{dashboard}\n 状态更新源: {source_tip} | 每 {interval} 秒刷新一次 (按 Ctrl+C 退出)")
                except Exception as loop_err:
                    clear_console()
                    print(f"正在等待 Antigravity 响应: {loop_err}...")
                time.sleep(interval)
        except KeyboardInterrupt:
            return 0

    # 单次运行模式
    try:
        snapshot, is_live = get_snapshot()
    except Exception as err:
        print(f"错误: 无法获取 Antigravity 额度: {err}", file=sys.stderr)
        return 1

    if args.json:
        data = snapshot.to_dict()
        data["is_live"] = is_live
        data["running_tasks"] = watcher.running_task_count
        print(json.dumps(data, ensure_ascii=False, indent=2))
        return 0

    dashboard = format_console_dashboard(
        snapshot,
        running_task_count=watcher.running_task_count,
        filter_keyword=args.filter,
    )
    if not is_live:
        dashboard += "\n [注意: 当前未能直连 Antigravity 后台，以上为本地历史快照滚动推算数据]"
    print(dashboard)
    return 0


if __name__ == "__main__":
    sys.exit(main())
