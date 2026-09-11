"""FoloToy AI Passport 硬件 BLE 蓝牙桥接模块。

将 Antigravity 的实时模型配额、任务执行状态与任务完成动效
通过 Nordic UART Service (NUS) BLE 协议同步至 FoloToy AI Passport 硬件。
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
from pathlib import Path
import sys
import time
from typing import Any, Dict, Optional

from .cache import DEFAULT_CACHE_PATH, load_cached_snapshot, roll_expired_quotas, save_cached_snapshot
from .client import AntigravityClient, UsageSnapshot
from .watcher import AntigravitySessionWatcher

logger = logging.getLogger(__name__)

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

HEARTBEAT_SECONDS = 10.0
USAGE_REFRESH_SECONDS = 60.0
TASK_REFRESH_SECONDS = 2.0


def build_ai_passport_payload(
    snapshot: UsageSnapshot,
    watcher: AntigravitySessionWatcher,
    completed: bool = False,
) -> bytes:
    """构建与 FoloToy AI Passport 协议完全兼容的心跳数据载荷。"""
    running_count = watcher.running_task_count
    message = (
        "任务已完成"
        if completed
        else ("助手正在工作" if running_count > 0 else "助手已就绪")
    )

    pm = snapshot.primary_model
    sm = snapshot.secondary_model

    pm_used = int(round(pm.used_percent)) if pm else 0
    pm_reset = pm.reset_timestamp if pm and pm.reset_timestamp else 0
    # 默认窗口（分钟）：Gemini 一般滚动 5 小时 = 300 分钟
    pm_window = 300

    sm_used = int(round(sm.used_percent)) if sm else 0
    sm_reset = sm.reset_timestamp if sm and sm.reset_timestamp else 0
    sm_window = 300

    payload: Dict[str, Any] = {
        "total": running_count,
        "running": running_count,
        "waiting": 0,
        "msg": message,
        "entries": [],
        "tokens": 0,
        "tokens_today": 0,
        # 兼容 codex-usage-ai-passport 固件的用量节点
        "codex": {
            "plan": snapshot.account.plan_name[:31],
            "primary_used": min(100, max(0, pm_used)),
            "primary_window": pm_window,
            "primary_reset": pm_reset,
            "secondary_used": min(100, max(0, sm_used)),
            "secondary_window": sm_window,
            "secondary_reset": sm_reset,
            "completion_seq": watcher.completion_sequence,
            "available": snapshot.available,
        },
        # 原生 Antigravity 专有扩展字段
        "antigravity": {
            "user": snapshot.account.name,
            "tier": snapshot.account.user_tier_name,
            "primary_label": pm.label if pm else "Gemini",
            "primary_remaining": pm.remaining_percent if pm else 100.0,
            "secondary_label": sm.label if sm else "Claude",
            "secondary_remaining": sm.remaining_percent if sm else 100.0,
        },
    }

    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"


async def send_payload(client: Any, payload: bytes) -> None:
    """将数据包分片发送至 AI Passport 的 NUS RX 特征。"""
    char = client.services.get_characteristic(NUS_RX_UUID)
    if char is None:
        raise RuntimeError("AI Passport 设备缺少 Nordic UART RX 特征")
    chunk_size = max(20, int(getattr(char, "max_write_without_response_size", 20)))
    for offset in range(0, len(payload), chunk_size):
        await client.write_gatt_char(
            NUS_RX_UUID, payload[offset : offset + chunk_size], response=False
        )


async def find_device(device_name: Optional[str]) -> Any:
    """扫描并发现附近的 AI Passport 蓝牙设备。"""
    try:
        from bleak import BleakScanner
    except ImportError:
        raise RuntimeError("未安装 bleak 库，请使用 pip install bleak 安装蓝牙支持。")

    print("正在扫描附近的 AI Passport 设备...", flush=True)
    devices = await BleakScanner.discover(timeout=8.0, service_uuids=[NUS_SERVICE_UUID])
    for dev in devices:
        name = dev.name or ""
        if device_name and (name == device_name or dev.address == device_name):
            return dev
        if not device_name and (
            name.startswith("Codex-")
            or name.startswith("Passport-")
            or "Passport" in name
        ):
            return dev
    raise RuntimeError(
        "未在周围发现任何 AI Passport (Codex-* / Passport-*) 设备，请确认设备已开机并开启蓝牙广播。"
    )


async def run_bridge(
    device_name: Optional[str] = None,
    dry_run: bool = False,
    cache_path: Path = DEFAULT_CACHE_PATH,
) -> None:
    """运行 AI Passport 蓝牙桥接主事件循环。"""
    client = AntigravityClient()
    watcher = AntigravitySessionWatcher()

    # 尝试加载缓存
    snapshot = load_cached_snapshot(cache_path)

    # 刷新最新额度
    try:
        live_snap = client.fetch_usage_snapshot()
        snapshot = live_snap
        save_cached_snapshot(snapshot, cache_path)
    except Exception as exc:
        print(f"提示: 直连 Antigravity 失败 ({exc})，使用历史缓存...", file=sys.stderr)
        if snapshot is None:
            raise RuntimeError("无法直连且无本地历史快照可用，请先启动 Antigravity IDE。") from exc

    roll_expired_quotas(snapshot)
    print(
        f"成功就绪: 主力模型={snapshot.primary_model.label} (剩余 {snapshot.primary_model.remaining_percent}%) | 运行任务={watcher.running_task_count}",
        flush=True,
    )

    # Dry-run 模式：仅输出载荷并退出
    if dry_run:
        payload = build_ai_passport_payload(snapshot, watcher)
        print("\n[AI Passport Dry-Run 载荷输出 (JSON)]:")
        print(payload.decode("utf-8").strip())
        return

    try:
        from bleak import BleakClient
    except ImportError:
        raise RuntimeError("未安装 bleak 库，请运行 pip install bleak")

    while True:
        try:
            device = await find_device(device_name)
            print(f"正在连接设备: {device.name or device.address} ...", flush=True)

            async with BleakClient(device, pair=True, timeout=60.0) as ble_client:
                await ble_client.start_notify(NUS_TX_UUID, lambda _s, _d: None)
                print("设备连接成功！Antigravity 额度与状态实时同步中...", flush=True)

                # 同步时区与时间
                tz_offset = int(time.mktime(time.localtime()) - time.mktime(time.gmtime()))
                time_payload = (
                    json.dumps({"time": [int(time.time()), tz_offset]}).encode("utf-8")
                    + b"\n"
                )
                await send_payload(ble_client, time_payload)

                last_heartbeat = 0.0
                last_usage_refresh = 0.0
                prev_sequence = watcher.completion_sequence

                while ble_client.is_connected:
                    changed = watcher.poll()
                    completed = watcher.completion_sequence > prev_sequence
                    prev_sequence = watcher.completion_sequence

                    now = time.monotonic()
                    if roll_expired_quotas(snapshot):
                        save_cached_snapshot(snapshot, cache_path)
                        changed = True

                    # 周期刷新用量 (默认60秒)
                    if now - last_usage_refresh >= USAGE_REFRESH_SECONDS:
                        try:
                            refreshed = client.fetch_usage_snapshot()
                            snapshot = refreshed
                            roll_expired_quotas(snapshot)
                            save_cached_snapshot(snapshot, cache_path)
                            changed = True
                        except Exception as refresh_err:
                            logger.debug("刷新额度失败: %s", refresh_err)
                        last_usage_refresh = now

                    # 当状态变化或到达心跳周期时发送报文
                    if changed or now - last_heartbeat >= HEARTBEAT_SECONDS or completed:
                        payload = build_ai_passport_payload(
                            snapshot, watcher, completed=completed
                        )
                        await send_payload(ble_client, payload)
                        last_heartbeat = now

                    await asyncio.sleep(1.0)
        except Exception as err:
            print(f"蓝牙连接断开: {err}。3 秒后尝试重连...", file=sys.stderr, flush=True)
            await asyncio.sleep(3.0)


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="antigravity-bridge",
        description="FoloToy AI Passport 硬件 BLE 蓝牙桥接器",
    )
    parser.add_argument(
        "--device",
        help="指定 AI Passport 设备的精确蓝牙名称或 MAC 地址",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="离线模拟模式：仅生成并打印即将推送到 AI Passport 的完整报文载荷",
    )
    args = parser.parse_args()

    try:
        asyncio.run(run_bridge(device_name=args.device, dry_run=args.dry_run))
        return 0
    except KeyboardInterrupt:
        print("\n已停止桥接服务。")
        return 0
    except Exception as err:
        print(f"桥接器运行错误: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
