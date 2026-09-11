#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FoloToy AI Passport 固件一键烧录工具
使用 esptool 将编译出的 full.bin 烧录至 ESP32-C3 物理板卡
"""

import sys
import argparse
import subprocess
from pathlib import Path
import serial.tools.list_ports

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "firmware" / "build"
DEFAULT_BIN = BUILD_DIR / "FoloToy-AI-Passport-full.bin"


def list_serial_ports():
    ports = list(serial.tools.list_ports.comports())
    return [p.device for p in ports]


def flash_firmware(port: str | None = None, bin_path: Path | None = None, baud: int = 460800):
    bin_file = bin_path or DEFAULT_BIN
    if not bin_file.is_file():
        print(f"[ERR] 未找到待烧录固件: {bin_file}")
        print("请先通过 GitHub Actions 或本地编译生成 full.bin 文件后再执行烧录。")
        return False

    if not port:
        available_ports = list_serial_ports()
        if not available_ports:
            print("[ERR] 未检测到任何串口设备，请使用 USB 数据线将 AI Passport 连接至电脑。")
            return False
        port = available_ports[0]
        print(f"[*] 自动选择检测到的串口: {port}")

    print(f"[*] 准备向 {port} 烧录固件: {bin_file.name} ({bin_file.stat().st_size} 字节)")
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

    print(f"-> 执行烧录命令: {' '.join(cmd)}")
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print(f"[OK] 固件烧录成功！设备已自动重启。")
        return True
    else:
        print("[ERR] 烧录失败，请检查串口占用或按住 BOOT 键重新插入 USB 尝试。")
        return False


def main():
    parser = argparse.ArgumentParser(description="FoloToy AI Passport 固件一键烧录脚本")
    parser.add_argument("-p", "--port", type=str, default=None, help="COM 端口号（如 COM3，默认自动检测）")
    parser.add_argument("-b", "--baud", type=int, default=460800, help="烧录波特率，默认 460800")
    parser.add_argument("--bin", type=str, default=None, help="指定固件路径，默认 firmware/build/FoloToy-AI-Passport-full.bin")
    args = parser.parse_args()

    bin_path = Path(args.bin).resolve() if args.bin else None
    success = flash_firmware(port=args.port, bin_path=bin_path, baud=args.baud)
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
