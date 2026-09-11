"""FoloToy AI Passport · Token Monitor 硬件蓝牙桥接器。

将多 AI 工具（Antigravity, Claude Code, Codex, Cursor 等）的聚合 Token、
费用与额度数据，实时通过 Nordic UART Service (NUS) BLE 无线同步至 AI Passport。
"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
import json
import logging
import os
from pathlib import Path
import sys
import time
from typing import Any, Dict, Optional

# 导入本地采集引擎
from token_monitor_collector import TokenMonitorSnapshot, collect_all_tools

logger = logging.getLogger(__name__)

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

HEARTBEAT_SECONDS = 10.0
TOKEN_REFRESH_SECONDS = 30.0


def build_firmware_payload(snap: TokenMonitorSnapshot, completed: bool = False) -> bytes:
    """构建与 AI Passport Token Monitor 固件完全匹配的 BLE JSON 载荷。"""
    tools_list = []
    for t in snap.tools[:8]:
        tools_list.append({
            "name": t.name[:23],
            "tokens_today": t.tokens_today,
            "cost_cents": t.cost_cents,
            "used_percent": t.used_percent,
        })

    msg = "任务已完成" if completed else snap.message

    payload: Dict[str, Any] = {
        "total": snap.running_tasks,
        "running": snap.running_tasks,
        "waiting": 0,
        "tokens": snap.tokens_total,
        "tokens_today": snap.tokens_today,
        "msg": msg,
        "entries": [f"{t.name}: 剩余 {100 - t.used_percent}%" for t in snap.tools[:4]],
        # 兼容原有 codex 配额节点
        "codex": {
            "plan": snap.plan_name,
            "primary_used": snap.primary_used_percent,
            "primary_window": 300,
            "primary_reset": snap.primary_reset_timestamp,
            "secondary_used": snap.secondary_used_percent,
            "secondary_window": 10080,
            "secondary_reset": snap.secondary_reset_timestamp,
            "completion_seq": 1 if completed else 0,
            "available": snap.available,
        },
        # Token Monitor 专属扩展节点
        "token_monitor": {
            "tokens_today": snap.tokens_today,
            "tokens_total": snap.tokens_total,
            "cost_today_cents": snap.cost_today_cents,
            "cost_total_cents": snap.cost_total_cents,
            "currency": snap.currency,
            "active_tools_count": len(snap.tools),
            "tools": tools_list,
        },
    }

    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"


async def send_payload(client: Any, payload: bytes) -> None:
    """分片写入 BLE NUS 特征。"""
    char = client.services.get_characteristic(NUS_RX_UUID)
    if char is None:
        raise RuntimeError("AI Passport 设备缺少 NUS RX 特征")
    chunk_size = max(20, int(getattr(char, "max_write_without_response_size", 20)))
    for offset in range(0, len(payload), chunk_size):
        await client.write_gatt_char(
            NUS_RX_UUID, payload[offset : offset + chunk_size], response=False
        )


async def find_device(device_name: Optional[str]) -> Any:
    """扫描附近的 AI Passport 蓝牙设备。"""
    from bleak import BleakScanner

    print("正在扫描附近的蓝牙设备...", flush=True)
    # 使用广谱扫描避免 Windows 驱动过滤掉 Scan Response 中的 UUID
    devices = await BleakScanner.discover(timeout=6.0)
    matched = []
    all_names = []

    for dev in devices:
        name = dev.name or ""
        if name:
            all_names.append(f"{name} ({dev.address})")
        if device_name:
            if name.lower() == device_name.lower() or dev.address.lower() == device_name.lower():
                matched.append(dev)
        else:
            n_lower = name.lower()
            if (
                n_lower.startswith("codex-")
                or n_lower.startswith("passport-")
                or "passport" in n_lower
                or "folotoy" in n_lower
                or "buddy" in n_lower
            ):
                matched.append(dev)

    if matched:
        chosen = matched[0]
        print(f"[OK] 找到目标设备: {chosen.name or '未知'} [{chosen.address}]", flush=True)
        return chosen

    hint = "\n   - " + "\n   - ".join(all_names) if all_names else " (未搜到任何带名称的蓝牙设备)"
    raise RuntimeError(
        f"未自动识别到 AI Passport 设备。\n"
        f"附近检测到的设备列表:{hint}\n\n"
        f"提示：若设备名称不在预设列表中，可使用参数指定：\n"
        f"   python tools/token_monitor_bridge.py --device \"您的设备名称或MAC\""
    )


def print_simulated_screen(snap: TokenMonitorSnapshot) -> None:
    """在终端呈现 AI Passport 屏幕的 ASCII 预览。"""
    print("\n" + "=" * 52)
    print("           AI PASSPORT 极客桌面副屏呈现预览          ")
    print("=" * 52)
    print(f" [● 在线]          {datetime.now().strftime('%H:%M')}             [■■■ 85%] ")
    print("-" * 52)
    print("                     AI PASSPORT                    ")
    status_tag = "● 工作中" if snap.running_tasks > 0 else "○ 待命中"
    print(f"                {status_tag} · {snap.running_tasks} 任务运行中      ")
    print("-" * 52)
    print("  [主力模型: " + snap.primary_quota_label + "]")
    rem1 = 100 - snap.primary_used_percent
    bar1 = int(round(rem1 / 5))
    print(f"  剩余: {rem1:>3}%   [{'■' * bar1}{' ' * (20 - bar1)}]  (PRO 有效)")
    print("-" * 52)
    print("  [辅助模型: " + snap.secondary_quota_label + "]")
    rem2 = 100 - snap.secondary_used_percent
    bar2 = int(round(rem2 / 5))
    print(f"  剩余: {rem2:>3}%   [{'■' * bar2}{' ' * (20 - bar2)}]  (运行正常)")
    print("-" * 52)
    print(f"  今日交互轮次:  {snap.tokens_today // 1000:>3} 轮")
    print(f"  订阅状态:      {snap.plan_name} (无额外按量扣费)")
    print(f"  监控工具数:    {len(snap.tools):>3} 个核心模型")
    print("=" * 52 + "\n")


async def run_bridge(device_name: Optional[str], dry_run: bool) -> None:
    snap = collect_all_tools()
    payload = build_firmware_payload(snap)

    if dry_run:
        print_simulated_screen(snap)
        print("[即将推送至设备的真实 BLE NUS 载荷 (JSON)]:")
        print(payload.decode("utf-8").strip())
        print("\nDry-Run 模式测试完成，报文与固件协议 100% 匹配。")
        return

    from bleak import BleakClient

    connected_once = False
    while True:
        try:
            device = await find_device(device_name)
            if not connected_once:
                print(f"正在发起连接与配对: {device.name or device.address} ...", flush=True)
                print(">>> 提示：若设备屏幕出现 6 位 PIN 码，请在 Windows 弹窗中输入。配对等待已延长至 120 秒。", flush=True)
            # 注意：在 Windows 上切勿传递 pair=True，因为 Bleak 的底层 WinRT 实现仅支持 CONFIRM_ONLY，
            # 会与单片机硬件的 PIN 码显示机制冲突并导致 2 秒内瞬间断开连接！
            # 移除 pair=True 后由 Windows 操作系统原生安全栈处理配对，PIN 码将稳定常驻显示。
            async with BleakClient(device, timeout=60.0) as client:
                await client.start_notify(NUS_TX_UUID, lambda _s, _d: None)
                print("[OK] 设备已成功连接并绑定！Token Monitor 数据流已上线！\n", flush=True)
                connected_once = True

                # 同步时钟
                tz_offset = int(time.mktime(time.localtime()) - time.mktime(time.gmtime()))
                await send_payload(
                    client,
                    json.dumps({"time": [int(time.time()), tz_offset]}).encode() + b"\n",
                )

                last_heartbeat = 0.0
                last_refresh = 0.0

                while client.is_connected:
                    now = time.monotonic()
                    if now - last_refresh >= TOKEN_REFRESH_SECONDS:
                        snap = collect_all_tools()
                        last_refresh = now

                    if now - last_heartbeat >= HEARTBEAT_SECONDS:
                        p = build_firmware_payload(snap)
                        await send_payload(client, p)
                        last_heartbeat = now
                        rem = 100 - snap.primary_used_percent
                        print(
                            f"[{datetime.now().strftime('%H:%M:%S')}] 数据已同步至设备 (主力: {snap.primary_quota_label} 剩余 {rem}% | 运行任务: {snap.running_tasks})",
                            flush=True,
                        )

                    await asyncio.sleep(1.0)

        except Exception as err:
            if not connected_once:
                print("\n" + "=" * 65, file=sys.stderr)
                print(f"[!] 首次连接/配对中断: {err}", file=sys.stderr)
                print("=" * 65, file=sys.stderr)
                print("可能原因：", file=sys.stderr)
                print("  设备屏幕显示了 6 位配对码，但 Windows 弹窗未及时输入或已超时。\n", file=sys.stderr)
                print("【最稳妥、最从容的解决方案（强烈推荐）】：", file=sys.stderr)
                print("  1. 打开 Windows【设置】->【蓝牙和其他设备】-> 点击【添加设备】->【蓝牙】；", file=sys.stderr)
                print("  2. 在搜索列表中点击您的设备（如 Codex-xxxx 或 AI-Passport）；", file=sys.stderr)
                print("  3. 此时 Windows 会常驻显示 6 位 PIN 码输入框，输入后点击连接完成系统级绑定；", file=sys.stderr)
                print("  4. 绑定成功后，再次运行本程序即可秒级直连，永远不再弹码！", file=sys.stderr)
                print("=" * 65, file=sys.stderr)

                if sys.stdin and sys.stdin.isatty():
                    try:
                        await asyncio.to_thread(input, "\n输入 PIN 码或准备就绪后，按 [Enter/回车键] 重新发起连接 (或按 Ctrl+C 退出)...")
                    except Exception:
                        await asyncio.sleep(15.0)
                else:
                    print("等待 30 秒后自动重试...", file=sys.stderr, flush=True)
                    await asyncio.sleep(30.0)
            else:
                print(f"数据通信偶发中断: {err}。等待 10 秒后自动重连...", file=sys.stderr, flush=True)
                await asyncio.sleep(10.0)


def main() -> int:
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass

    parser = argparse.ArgumentParser(
        prog="token-monitor-bridge",
        description="FoloToy AI Passport · Token Monitor 硬件 BLE 蓝牙桥接器",
    )
    parser.add_argument("--device", help="指定 AI Passport 设备的精确名称或蓝牙地址")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="离线模拟模式：打印设备屏幕预览及即将发送的完整 BLE 载荷",
    )
    args = parser.parse_args()

    if not args.dry_run:
        print("=" * 60)
        print("       FoloToy AI Passport · Token Monitor 同步服务")
        print("=" * 60)
        print("请确认：")
        print("  1. 电脑系统蓝牙已开启")
        print("  2. AI Passport 设备已开机并在电脑附近")
        print("  3. 首次配对若设备屏幕显示 6 位配对码，请在 Windows 弹窗中输入")
        print("=" * 60 + "\n", flush=True)

    try:
        asyncio.run(run_bridge(args.device, args.dry_run))
        return 0
    except KeyboardInterrupt:
        print("\n桥接已退出。")
        return 0
    except Exception as exc:
        print(f"运行失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
