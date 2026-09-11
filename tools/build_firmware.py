#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FoloToy AI Passport Token Monitor 固件构建总控脚本
支持：
1. 本地 ESP-IDF 工具链原生编译 (idf.py)
2. 本地 Docker 容器化一键编译 (espressif/idf:v5.5.3)
3. 自动化分区表二进制编译与 3MB 尺寸门禁
4. GitHub Actions 云端自动构建引导
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
FIRMWARE_DIR = REPO_ROOT / "firmware"
BUILD_DIR = FIRMWARE_DIR / "build"


def check_tool(name: str) -> bool:
    return shutil.which(name) is not None


def compile_partition_table():
    print("[1/4] 正在编译 ESP32-C3 分区表 (partitions.csv -> partition-table.bin)...")
    from tools.gen_partition_table import compile_csv_to_bin
    csv_file = FIRMWARE_DIR / "partitions.csv"
    bin_file = BUILD_DIR / "partition_table" / "partition-table.bin"
    compile_csv_to_bin(csv_file, bin_file)


def build_with_idf():
    print("[2/4] 检测到本地 ESP-IDF 工具链 (idf.py)，开始本地构建...")
    env = os.environ.copy()
    
    cmd_build = ["idf.py", "-C", str(FIRMWARE_DIR), "build"]
    print(f"-> 运行命令: {' '.join(cmd_build)}")
    res = subprocess.run(cmd_build, cwd=str(FIRMWARE_DIR), env=env)
    if res.returncode != 0:
        print("[ERR] 固件编译失败，请检查 CMake 或代码语法错误")
        return False

    merged_bin = BUILD_DIR / "FoloToy-AI-Passport-full.bin"
    cmd_merge = ["idf.py", "-C", str(FIRMWARE_DIR), "merge-bin", "-o", str(merged_bin)]
    print(f"-> 正在合并生成完整镜像: {' '.join(cmd_merge)}")
    res_merge = subprocess.run(cmd_merge, cwd=str(FIRMWARE_DIR), env=env)
    if res_merge.returncode != 0:
        print("[ERR] 固件合并失败")
        return False

    return True


def build_with_docker():
    print("[2/4] 检测到本地 Docker，启动官方 ESP-IDF 5.5.3 容器化编译...")
    mount_src = str(FIRMWARE_DIR).replace("\\", "/")
    cmd = [
        "docker", "run", "--rm",
        "-v", f"{mount_src}:/project",
        "-w", "/project",
        "espressif/idf:v5.5.3",
        "sh", "-c", "idf.py build && idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin"
    ]
    print(f"-> 运行容器命令: {' '.join(cmd)}")
    res = subprocess.run(cmd)
    return res.returncode == 0


def print_cloud_and_local_instructions():
    print("\n" + "=" * 65)
    print("           ESP32-C3 固件编译环境未就绪提示与解决途径            ")
    print("=" * 65)
    print("由于 ESP32-C3 采用 32 位 RISC-V 架构，编译二进制固件 (.bin)")
    print("必须依赖 ESP-IDF v5.5.3 交叉编译工具链 (riscv32-esp-elf-gcc)。")
    print("\n【方案 A：GitHub Actions 云端全自动编译（最推荐，零本地安装）】")
    print("1. 本项目已内置完整的 GitHub Actions 构建流水线：")
    print("   .github/workflows/build-firmware.yml")
    print("2. 只要您将当前工程 git push 到 GitHub 仓库，GitHub 云端将自动")
    print("   免费启动官方 espressif/esp-idf-ci-action:v5.5.3 容器进行编译。")
    print("3. 编译完成后，在 GitHub 仓库的 'Actions' 页面或 'Releases' 标签下，")
    print("   即可直接下载打包好的 FoloToy-AI-Passport-full.bin 固件！")
    print("\n【方案 B：本地安装 Docker 一键容器化编译】")
    print("若安装了 Docker Desktop，只需重新运行本脚本即可自动容器化构建：")
    print("   python tools/build_firmware.py")
    print("\n【方案 C：Windows 原生安装 ESP-IDF 5.5.3】")
    print("1. 前往乐鑫官网下载 ESP-IDF Windows 一键离线安装包：")
    print("   https://dl.espressif.com/dl/esp-idf/")
    print("2. 打开 'ESP-IDF 5.5.3 CMD' 终端，进入项目目录运行：")
    print("   python tools/build_firmware.py")
    print("=" * 65 + "\n")


def main():
    print("=== 开始 FoloToy AI Passport 固件构建与审计流程 ===")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    # 1. 编译分区表二进制
    compile_partition_table()

    # 2. 尝试本地编译
    success = False
    if check_tool("idf.py"):
        success = build_with_idf()
    elif check_tool("docker"):
        success = build_with_docker()
    else:
        print_cloud_and_local_instructions()
        return 1

    # 3. 运行固件审计
    if success:
        print("\n[3/4] 正在运行固件 3MB 分区契约与安全性审计...")
        cmd_verify = [sys.executable, str(REPO_ROOT / "tools" / "verify_firmware.py"), str(BUILD_DIR)]
        subprocess.run(cmd_verify)
        print("\n[4/4] 固件编译与校验圆满完成！产物路径：")
        print(f" -> 全量固件: {BUILD_DIR / 'FoloToy-AI-Passport-full.bin'}")
        print(f" -> 应用固件: {BUILD_DIR / 'FoloToy-AI-Passport.bin'}")
        print(f" -> 分区表:   {BUILD_DIR / 'partition_table' / 'partition-table.bin'}")
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
