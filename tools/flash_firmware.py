#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FoloToy AI Passport 固件一键烧录工具。
使用 esptool 将编译出的 full.bin 烧录至 ESP32-C3 物理板卡。
"""

import argparse
from pathlib import Path
import subprocess
import sys
import serial.tools.list_ports

# 确保在 Windows 控制台输出不乱码且不崩溃
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(errors="replace")
        sys.stderr.reconfigure(errors="replace")
    except Exception:
        pass

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "firmware" / "build"
DEFAULT_BIN = BUILD_DIR / "FoloToy-AI-Passport-full.bin"


def detect_target_port() -> tuple[str | None, str]:
    """智能检测 AI Passport 的 ESP32-C3 串口设备。"""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None, "未检测到任何串口设备"

    # 1. 优先寻找 Espressif 官方 USB 硬件ID (VID: 0x303A)
    for p in ports:
        hwid = (p.hwid or "").upper()
        if "303A:" in hwid:
            return p.device, f"Espressif 原生硬件设备 ({p.description})"

    # 2. 其次寻找带有 USB 标识的串口 (排除主板 COM1 等物理端口)
    for p in ports:
        desc = (p.description or "").upper()
        hwid = (p.hwid or "").upper()
        if p.device.upper() != "COM1" and ("USB" in desc or "USB" in hwid or "CP210" in desc or "CH340" in desc):
            return p.device, f"USB 串口设备 ({p.description})"

    # 3. 兜底寻找非 COM1 设备
    for p in ports:
        if p.device.upper() != "COM1":
            return p.device, p.description

    # 若仅有 COM1，返回并提示
    return ports[0].device, ports[0].description


def flash_firmware(port: str | None = None, bin_path: Path | None = None, baud: int = 460800) -> bool:
    bin_file = bin_path or DEFAULT_BIN
    if not bin_file.is_file():
        print(f"[ERR] 未找到待烧录固件: {bin_file}")
        print("请确认固件已成功下载至 firmware/build/ 目录。")
        return False

    port_desc = ""
    if not port:
        port, port_desc = detect_target_port()
        if not port:
            print("[ERR] 未检测到任何串口设备，请使用 USB 数据线将 AI Passport 连接至电脑。")
            return False
        print(f"[*] 智能识别到目标串口: {port} [{port_desc}]")

    print(f"[*] 准备向 {port} 烧录全新极客固件: {bin_file.name} ({bin_file.stat().st_size} 字节, {bin_file.stat().st_size / 1024:.1f} KB)")
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32c3",
        "--port", port,
        "--baud", str(baud),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "8MB",
        "0x0000", str(bin_file)
    ]

    print(f"[*] 正在通过 esptool 进行烧录写入，请保持 USB 连接稳定...")
    try:
        res = subprocess.run(cmd)
        if res.returncode == 0:
            print("\n" + "=" * 52)
            print("[SUCCESS] 烧录成功！设备已自动重启，全新极客界面已就绪！")
            print("=" * 52)
            return True
        else:
            print(f"\n[ERR] 烧录过程退出，错误码: {res.returncode}")
            print("提示：若提示连接超时，可按住机身按键后重新插拔 USB 数据线再次尝试。")
            return False
    except Exception as exc:
        print(f"[ERR] 执行烧录异常: {exc}")
        return False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="FoloToy AI Passport 固件一键烧录工具")
    parser.add_argument("--port", "-p", default=None, help="目标串口 (如 COM3)")
    parser.add_argument("--bin", "-b", default=None, type=Path, help="待烧录 bin 固件路径")
    parser.add_argument("--baud", default=460800, type=int, help="波特率 (默认 460800)")
    args = parser.parse_args()

    success = flash_firmware(args.port, args.bin, args.baud)
    sys.exit(0 if success else 1)
