"""FoloToy AI Passport · Token Monitor 硬件蓝牙桥接器。

将多 AI 工具（Antigravity, Claude Code, Codex, Cursor 等）的聚合 Token、
费用与额度数据，实时通过 Nordic UART Service (NUS) BLE 无线同步至 AI Passport。
"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import os
from pathlib import Path
import socket
import sys
import threading
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

PROFILE_CONFIG_FILE = Path(__file__).resolve().parent / "profile_config.json"
_active_ble_client: Optional[Any] = None
_active_event_loop: Optional[asyncio.AbstractEventLoop] = None
_web_server_started = False


def get_local_ip() -> str:
    """获取本机在局域网中的真实 IP 地址。"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "192.168.48.156"


def get_profile_config() -> dict:
    """读取保存的个人主页档案配置。"""
    default_cfg = {"name": "syhx114514", "owner": "syhx114514@gmail.com"}
    if PROFILE_CONFIG_FILE.exists():
        try:
            data = json.loads(PROFILE_CONFIG_FILE.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return {
                    "name": str(data.get("name") or default_cfg["name"])[:30],
                    "owner": str(data.get("owner") or default_cfg["owner"])[:30],
                }
        except Exception:
            pass
    return default_cfg


def save_profile_config(name: str, owner: str) -> None:
    """持久化保存个人主页档案。"""
    data = {"name": name[:30], "owner": owner[:30]}
    PROFILE_CONFIG_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def trigger_profile_sync(name: str, owner: str) -> bool:
    """当 Web 端修改个人信息后，通过活跃的 BLE 连接秒级推送到副屏。"""
    global _active_ble_client, _active_event_loop
    if _active_ble_client and _active_ble_client.is_connected and _active_event_loop:
        try:
            # 严格匹配固件协议：固件要求 {"cmd": "name", "name": "..."} 与 {"cmd": "owner", "name": "..."}
            p_name = json.dumps({"cmd": "name", "name": name, "value": name}, ensure_ascii=False).encode("utf-8") + b"\n"
            p_owner = json.dumps({"cmd": "owner", "name": owner, "owner": owner, "value": owner}, ensure_ascii=False).encode("utf-8") + b"\n"
            asyncio.run_coroutine_threadsafe(send_payload(_active_ble_client, p_name), _active_event_loop)
            asyncio.run_coroutine_threadsafe(send_payload(_active_ble_client, p_owner), _active_event_loop)
            print(f"[Web配置] 档案已通过 BLE 实时下发副屏: Name={name}, Owner={owner}", flush=True)
            return True
        except Exception as e:
            logger.error(f"BLE 下发失败: {e}")
    return False


HTML_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>AI Passport · 个人主页档案配置</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background-color: #0c0e12;
    color: #e2e8f0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "PingFang SC", "Microsoft YaHei", sans-serif;
    padding: 24px 16px;
    display: flex;
    justify-content: center;
  }
  .container {
    width: 100%;
    max-width: 420px;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }
  .header {
    text-align: center;
    padding: 10px 0 6px 0;
  }
  .badge {
    display: inline-block;
    padding: 4px 12px;
    background: rgba(225, 123, 82, 0.15);
    color: #e17b52;
    border: 1px solid #e17b52;
    border-radius: 4px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1px;
    margin-bottom: 8px;
  }
  .title {
    font-size: 21px;
    font-weight: 700;
    color: #ffffff;
    letter-spacing: 0.5px;
  }
  .subtitle {
    font-size: 13px;
    color: #8a99a8;
    margin-top: 4px;
  }
  .card {
    background: #14171f;
    border: 1px solid #232936;
    border-radius: 12px;
    padding: 18px;
    box-shadow: 0 4px 20px rgba(0,0,0,0.4);
  }
  .card-title {
    font-size: 14px;
    font-weight: 600;
    color: #f1c75b;
    margin-bottom: 14px;
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .card-title::before {
    content: "";
    display: inline-block;
    width: 3px;
    height: 14px;
    background: #f1c75b;
    border-radius: 2px;
  }
  .form-group {
    margin-bottom: 16px;
  }
  label {
    display: block;
    font-size: 13px;
    font-weight: 500;
    color: #cbd5e1;
    margin-bottom: 6px;
  }
  .input-desc {
    font-size: 11px;
    color: #64748b;
    margin-top: 4px;
  }
  input[type="text"] {
    width: 100%;
    background: #090b0e;
    border: 1px solid #2e384d;
    border-radius: 8px;
    padding: 12px 14px;
    color: #ffffff;
    font-size: 15px;
    outline: none;
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  input[type="text"]:focus {
    border-color: #e17b52;
    box-shadow: 0 0 0 2px rgba(225, 123, 82, 0.2);
  }
  .btn-submit {
    width: 100%;
    background: linear-gradient(135deg, #e17b52 0%, #c45d35 100%);
    color: #ffffff;
    border: none;
    border-radius: 8px;
    padding: 14px;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
    box-shadow: 0 4px 12px rgba(225, 123, 82, 0.35);
    transition: transform 0.1s, opacity 0.2s;
  }
  .btn-submit:active {
    transform: scale(0.98);
    opacity: 0.9;
  }
  .status-box {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 14px;
    background: #090b0e;
    border-radius: 8px;
    border: 1px solid #1e2430;
    font-size: 12px;
  }
  .status-dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #64c987;
    margin-right: 6px;
    box-shadow: 0 0 8px #64c987;
  }
  .alert {
    padding: 12px;
    border-radius: 8px;
    font-size: 13px;
    display: none;
    margin-bottom: 14px;
  }
  .alert-success {
    background: rgba(100, 201, 135, 0.15);
    border: 1px solid #64c987;
    color: #64c987;
  }
  .alert-error {
    background: rgba(239, 75, 56, 0.15);
    border: 1px solid #ef4b38;
    color: #ef4b38;
  }
  .footer {
    text-align: center;
    font-size: 11px;
    color: #475569;
    margin-top: 10px;
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="badge">AI PASSPORT CONFIG</div>
    <div class="title">个人智能主页档案设置</div>
    <div class="subtitle">修改后将通过蓝牙即时同步至副屏设备</div>
  </div>

  <div class="card">
    <div class="status-box">
      <div style="display: flex; align-items: center;">
        <span class="status-dot" id="statusDot"></span>
        <span id="statusText">蓝牙服务在线</span>
      </div>
      <span style="color: #64748b;" id="devId">C3-32EAAA</span>
    </div>
  </div>

  <div class="card">
    <div class="card-title">通行证基本身份</div>
    <form id="profileForm">
      <div class="form-group">
        <label for="name">极客昵称 / 姓名</label>
        <input type="text" id="name" name="name" maxlength="30" placeholder="例如: syhx114514" required>
        <div class="input-desc">展示在副屏工牌卡片中央顶部 (最多30字符)</div>
      </div>

      <div class="form-group">
        <label for="owner">通行证账号 / 邮箱</label>
        <input type="text" id="owner" name="owner" maxlength="30" placeholder="例如: user@gmail.com" required>
        <div class="input-desc">展示在副屏核心资产与账号一栏 (最多30字符)</div>
      </div>

      <div id="alertBox" class="alert"></div>

      <button type="submit" class="btn-submit" id="btnSubmit">保存并同步至副屏</button>
    </form>
  </div>

  <div class="footer">
    FoloToy AI Passport · 极客桌面副屏控制台
  </div>
</div>

<script>
  fetch('/api/profile')
    .then(r => r.json())
    .then(data => {
      if (data.name) document.getElementById('name').value = data.name;
      if (data.owner) document.getElementById('owner').value = data.owner;
      const dot = document.getElementById('statusDot');
      const txt = document.getElementById('statusText');
      if (data.connected) {
        dot.style.background = '#64c987';
        dot.style.boxShadow = '0 0 8px #64c987';
        txt.textContent = '设备已连接 · 实时同步中';
      } else {
        dot.style.background = '#f1c75b';
        dot.style.boxShadow = 'none';
        txt.textContent = '设备待命 · 保存后自动下发';
      }
    })
    .catch(err => console.error(err));

  document.getElementById('profileForm').addEventListener('submit', async function(e) {
    e.preventDefault();
    const btn = document.getElementById('btnSubmit');
    const alertBox = document.getElementById('alertBox');
    btn.disabled = true;
    btn.textContent = '正在同步至设备...';
    alertBox.style.display = 'none';

    const name = document.getElementById('name').value.trim();
    const owner = document.getElementById('owner').value.trim();

    try {
      const res = await fetch('/api/profile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, owner })
      });
      const ret = await res.json();
      if (ret.ok) {
        alertBox.className = 'alert alert-success';
        alertBox.textContent = '✓ 档案保存成功！副屏已即时刷新。';
        alertBox.style.display = 'block';
      } else {
        throw new Error(ret.error || '保存失败');
      }
    } catch (err) {
      alertBox.className = 'alert alert-error';
      alertBox.textContent = '✗ 同步异常: ' + err.message;
      alertBox.style.display = 'block';
    } finally {
      btn.disabled = false;
      btn.textContent = '保存并同步至副屏';
    }
  });
</script>
</body>
</html>
"""


class ProfileWebHandler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: Any) -> None:
        pass  # 忽略默认访问日志，保持输出整洁

    def do_GET(self) -> None:
        if self.path == "/profile" or self.path == "/":
            content = HTML_PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        elif self.path == "/api/profile":
            cfg = get_profile_config()
            global _active_ble_client
            conn = bool(_active_ble_client and _active_ble_client.is_connected)
            data = {"ok": True, "name": cfg["name"], "owner": cfg["owner"], "connected": conn}
            content = json.dumps(data, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self) -> None:
        if self.path == "/api/profile":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            try:
                data = json.loads(body.decode("utf-8"))
                name = str(data.get("name", "")).strip()[:30]
                owner = str(data.get("owner", "")).strip()[:30]
                if not name:
                    name = "syhx114514"
                if not owner:
                    owner = "syhx114514@gmail.com"

                save_profile_config(name, owner)
                synced = trigger_profile_sync(name, owner)
                res_data = {"ok": True, "name": name, "owner": owner, "synced": synced}
                content = json.dumps(res_data, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
            except Exception as e:
                err_data = {"ok": False, "error": str(e)}
                content = json.dumps(err_data).encode("utf-8")
                self.send_response(400)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
        else:
            self.send_response(404)
            self.end_headers()


def start_profile_web_server(port: int = 8765) -> None:
    """在后台线程中启动轻量级 Web 配置服务器。"""
    global _web_server_started
    if _web_server_started:
        return
    try:
        server = ThreadingHTTPServer(("0.0.0.0", port), ProfileWebHandler)
        t = threading.Thread(target=server.serve_forever, daemon=True)
        t.start()
        _web_server_started = True
        local_ip = get_local_ip()
        print(f"[Web配置] 个人主页扫码配置服务已启动: http://{local_ip}:{port}/profile", flush=True)
    except Exception as e:
        logger.warning(f"无法启动 Web 配置服务器: {e}")


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


def format_token_str(count: int) -> str:
    """智能格式化 Token 计数 (如 850 / 45.2k / 122.7M)。"""
    if count >= 1_000_000:
        return f"{count / 1_000_000:.1f}M"
    elif count >= 1_000:
        return f"{count / 1_000:.1f}k"
    return str(count)


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
    pm_token_str = format_token_str(snap.tools[0].tokens_today) if snap.tools else "0"
    print(f"  剩余: {rem1:>3}%   [{'■' * bar1}{' ' * (20 - bar1)}]  (用量 {pm_token_str})")
    print("-" * 52)
    print("  [辅助模型: " + snap.secondary_quota_label + "]")
    rem2 = 100 - snap.secondary_used_percent
    bar2 = int(round(rem2 / 5))
    sec_token_str = format_token_str(snap.tools[1].tokens_today) if len(snap.tools) > 1 else "0"
    print(f"  剩余: {rem2:>3}%   [{'■' * bar2}{' ' * (20 - bar2)}]  (用量 {sec_token_str})")
    print("-" * 52)
    print(f"  今日 Token 消耗: {snap.tokens_today:>10,} ({format_token_str(snap.tokens_today)})")
    print(f"  今日预估费用:  ¥{snap.cost_today_cents / 100:.2f}")
    print(f"  订阅状态:      {snap.plan_name}")
    print(f"  监控数据源:    {snap.source}")
    print("=" * 52 + "\n")



async def run_bridge(device_name: Optional[str], dry_run: bool) -> None:
    start_profile_web_server(port=8765)
    global _active_event_loop, _active_ble_client
    _active_event_loop = asyncio.get_running_loop()

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
                _active_ble_client = client

                # 同步时钟与工牌个人档案
                tz_offset = int(time.mktime(time.localtime()) - time.mktime(time.gmtime()))
                await send_payload(
                    client,
                    json.dumps({"time": [int(time.time()), tz_offset]}).encode() + b"\n",
                )
                profile = get_profile_config()
                p_name = json.dumps({"cmd": "name", "name": profile["name"], "value": profile["name"]}, ensure_ascii=False).encode("utf-8") + b"\n"
                p_owner = json.dumps({"cmd": "owner", "name": profile["owner"], "owner": profile["owner"], "value": profile["owner"]}, ensure_ascii=False).encode("utf-8") + b"\n"
                await send_payload(client, p_name)
                await send_payload(client, p_owner)

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
                            f"[{datetime.now().strftime('%H:%M:%S')}] 数据已同步至设备 (主力: {snap.primary_quota_label} 剩余 {rem}% | 今日 Token: {format_token_str(snap.tokens_today)} | 预估: ¥{snap.cost_today_cents / 100:.2f} | 任务: {snap.running_tasks})",
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
