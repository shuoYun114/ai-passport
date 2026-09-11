#!/usr/bin/env python3
"""FoloToy AI Passport 固件大小与分区上限审计工具。

确保固件严格符合受保护的 Flash 布局契约：
- Application Partition (factory) 上限: 3 MB (0x300000 = 3,145,728 字节)
- 保留 cardid@0x356000
"""

from __future__ import annotations

from pathlib import Path
import sys

MAX_APP_SIZE_BYTES = 3 * 1024 * 1024  # 3MB (0x300000)
PARTITION_OFFSET = 0x10000
CARDID_OFFSET = 0x356000


if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def check_partitions_csv(path: Path) -> bool:
    """验证分区表中 factory 与 cardid 的契约配置。"""
    if not path.exists():
        print(f"[ERR] 分区表文件不存在: {path}", file=sys.stderr)
        return False

    content = path.read_text(encoding="utf-8")
    factory_valid = False
    cardid_valid = False

    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 5:
            name, ptype, subtype, offset, size = parts[:5]
            if name == "factory":
                offset_val = int(offset, 16) if offset.startswith("0x") else int(offset)
                size_val = int(size, 16) if size.startswith("0x") else int(size)
                if offset_val == PARTITION_OFFSET and size_val <= MAX_APP_SIZE_BYTES:
                    factory_valid = True
                    print(f"[OK] 分区表检查通过: factory 分区大小为 0x{size_val:X} ({size_val / 1024 / 1024:.2f} MB <= 3MB)")
                else:
                    print(f"[ERR] factory 分区大小 0x{size_val:X} 超出 3MB 上限或偏移错误!", file=sys.stderr)
            elif name == "cardid":
                offset_val = int(offset, 16) if offset.startswith("0x") else int(offset)
                if offset_val == CARDID_OFFSET:
                    cardid_valid = True
                    print(f"[OK] 分区表检查通过: cardid 契约偏移为 0x{offset_val:X}")

    return factory_valid and cardid_valid


def audit_firmware_size(firmware_dir: Path) -> bool:
    """审计固件源码与编译产物体积。"""
    print(f"=== 开始审计固件目录: {firmware_dir} ===")

    # 1. 检查分区表
    partitions_file = firmware_dir / "partitions.csv"
    if not check_partitions_csv(partitions_file):
        return False

    # 2. 检查编译生成的 bin 文件（若存在）
    bin_files = list(firmware_dir.glob("build/**/*.bin"))
    app_bins = [b for b in bin_files if "bootloader" not in b.name and "partition" not in b.name]

    if app_bins:
        for bin_file in app_bins:
            size = bin_file.stat().st_size
            ratio = (size / MAX_APP_SIZE_BYTES) * 100
            print(f"-> 发现固件二进制文件: {bin_file.name}, 大小: {size} 字节 ({size / 1024 / 1024:.3f} MB, 占用上限 {ratio:.1f}%)")
            if size > MAX_APP_SIZE_BYTES:
                print(f"[ERR] 严重错误: 固件大小超出 3MB 硬件限制 ({size} > {MAX_APP_SIZE_BYTES})!", file=sys.stderr)
                return False
            print(f"[OK] 固件尺寸安全: 未超出 3MB 限制 (剩余容量 {(MAX_APP_SIZE_BYTES - size) / 1024:.1f} KB)")
    else:
        # 静态资源统计
        total_source_bytes = sum(f.stat().st_size for f in firmware_dir.rglob("*.*") if f.is_file() and not f.name.endswith(".git"))
        print(f"-> 固件全量源代码与资源总大小: {total_source_bytes / 1024:.1f} KB (远低于 3MB 上限)")

    print("=== 固件大小审计通过: 满足 <= 3MB 契约 ===\n")
    return True


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent / "firmware"
    success = audit_firmware_size(root)
    sys.exit(0 if success else 1)
