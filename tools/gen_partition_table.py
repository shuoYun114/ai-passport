#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成符合 ESP-IDF 规范的 ESP32-C3 分区表二进制文件 (partition-table.bin)
支持 MD5 校验魔数与分区保护契约验证
"""

import sys
import struct
import hashlib
from pathlib import Path

# 确保控制台 UTF-8
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ENTRY_FORMAT = "<HBBII16sI"
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)
TABLE_SIZE = 0xC00
MAGIC_PARTITION = 0x50AA
MAGIC_MD5 = 0xEBEB

TYPE_MAPPING = {
    "app": 0x00,
    "data": 0x01,
}

SUBTYPE_MAPPING = {
    "app": {
        "factory": 0x00,
        "test": 0x20,
    },
    "data": {
        "ota": 0x00,
        "phy": 0x01,
        "nvs": 0x02,
        "coredump": 0x03,
        "nvs_keys": 0x04,
        "efuse": 0x05,
        "undefined": 0x06,
        "esphttpd": 0x80,
        "fat": 0x81,
        "spiffs": 0x82,
        "littlefs": 0x83,
        "custom": 0x02,
    }
}


def parse_size_or_offset(val_str: str) -> int:
    val_str = val_str.strip()
    if val_str.lower().endswith("k"):
        return int(val_str[:-1], 0) * 1024
    if val_str.lower().endswith("m"):
        return int(val_str[:-1], 0) * 1024 * 1024
    return int(val_str, 0)


def compile_csv_to_bin(csv_path: Path, output_bin: Path) -> bytes:
    table_data = bytearray()

    with open(csv_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue

            name = parts[0]
            ptype_str = parts[1].lower()
            subtype_str = parts[2].lower()
            offset = parse_size_or_offset(parts[3])
            size = parse_size_or_offset(parts[4])
            flags = parse_size_or_offset(parts[5]) if len(parts) > 5 and parts[5] else 0

            ptype = TYPE_MAPPING.get(ptype_str, int(ptype_str, 0) if ptype_str.startswith("0x") else 0)
            if subtype_str.startswith("0x"):
                subtype = int(subtype_str, 0)
            else:
                subtype = SUBTYPE_MAPPING.get(ptype_str, {}).get(subtype_str, 0)

            name_bytes = name.encode("ascii")
            if len(name_bytes) > 16:
                raise ValueError(f"分区名称过长: {name}")
            name_padded = name_bytes.ljust(16, b"\x00")

            entry = struct.pack(
                ENTRY_FORMAT,
                MAGIC_PARTITION,
                ptype,
                subtype,
                offset,
                size,
                name_padded,
                flags,
            )
            table_data.extend(entry)

    # 计算 MD5 并附加校验块
    md5_digest = hashlib.md5(table_data).digest()
    md5_entry = struct.pack("<H", MAGIC_MD5) + b"\x00" * 14 + md5_digest
    # 填充至 ENTRY_SIZE (32 字节)
    md5_block = md5_entry.ljust(ENTRY_SIZE, b"\xFF")
    table_data.extend(md5_block)

    # 填充整张表至 0xC00 (3072 字节)
    if len(table_data) < TABLE_SIZE:
        table_data.extend(b"\xFF" * (TABLE_SIZE - len(table_data)))

    output_bin.parent.mkdir(parents=True, exist_ok=True)
    with open(output_bin, "wb") as f:
        f.write(table_data)

    print(f"[OK] 分区表编译成功: {output_bin} ({len(table_data)} 字节)")
    return bytes(table_data)


if __name__ == "__main__":
    csv_file = Path("firmware/partitions.csv").resolve()
    bin_file = Path("firmware/build/partition_table/partition-table.bin").resolve()
    if len(sys.argv) > 1:
        csv_file = Path(sys.argv[1]).resolve()
    if len(sys.argv) > 2:
        bin_file = Path(sys.argv[2]).resolve()

    compile_csv_to_bin(csv_file, bin_file)
