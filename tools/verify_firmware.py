#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
校验经 idf.py merge-bin 合并生成的 ESP32-C3 固件镜像与分区保护契约
严格确保：
1. factory 分区大小不超过 3MB (0x300000)
2. cardid 保护区 (0x356000, 16KB) 未被破坏且不包含真实私钥数据
3. 镜像总大小不超过 8MB Flash 物理限制
"""

from __future__ import annotations

import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

# 保证 Windows 控制台 UTF-8 兼容
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

EXPECTED_IMAGES = (
    (0x0000, "bootloader/bootloader.bin"),
    (0x8000, "partition_table/partition-table.bin"),
    (0x10000, "FoloToy-AI-Passport.bin"),
)

FLASH_SIZE = 8 * 1024 * 1024
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0xC00
APP_MAX_SIZE = 0x300000  # 3MB 严格限制
CARDID_OFFSET = 0x356000
CARDID_SIZE = 0x4000
ENTRY = struct.Struct("<HBBII16sI")


@dataclass(frozen=True)
class Partition:
    kind: int
    subtype: int
    offset: int
    size: int
    label: str

    @property
    def end(self) -> int:
        return self.offset + self.size


def parse_partition_table(raw: bytes) -> tuple[list[Partition], bool]:
    """解析 ESP-IDF 分区表并验证 MD5 校验和"""
    if len(raw) < PARTITION_TABLE_SIZE:
        raise ValueError("分区表数据被截断，长度不足 0xC00 字节")

    partitions: list[Partition] = []
    found_md5 = False
    for cursor in range(0, PARTITION_TABLE_SIZE, ENTRY.size):
        magic = int.from_bytes(raw[cursor : cursor + 2], "little")
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            expected = hashlib.md5(raw[:cursor]).digest()
            actual = raw[cursor + 16 : cursor + 32]
            if actual != expected:
                raise ValueError("分区表 MD5 校验标记不匹配")
            found_md5 = True
            break
        if magic != 0x50AA:
            raise ValueError(f"分区表 0x{cursor:x} 处条目魔数无效")

        _, kind, subtype, offset, size, label_raw, _ = ENTRY.unpack_from(raw, cursor)
        label = label_raw.split(b"\0", 1)[0].decode("ascii", "strict")
        if not label or not size or offset < 0x9000 or offset + size > FLASH_SIZE:
            raise ValueError(f"分区 {label!r} 边界越界")
        partitions.append(Partition(kind, subtype, offset, size, label))

    if not partitions:
        raise ValueError("分区表为空")
    return partitions, found_md5


def verify_protected_layout(merged: bytes, build_dir: Path) -> None:
    """验证保护分区与 3MB 契约"""
    table = merged[
        PARTITION_TABLE_OFFSET : PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE
    ]
    partitions, found_md5 = parse_partition_table(table)
    if not found_md5:
        raise ValueError("分区表缺少合法的 MD5 标记")

    by_label = {item.label: item for item in partitions}
    expected = {
        "factory": Partition(0, 0, 0x10000, APP_MAX_SIZE, "factory"),
        "cardid": Partition(1, 2, CARDID_OFFSET, CARDID_SIZE, "cardid"),
    }
    for label, wanted in expected.items():
        if by_label.get(label) != wanted:
            raise ValueError(f"分区 {label!r} 必须为 {wanted}，实际为 {by_label.get(label)}")

    ordered = sorted(partitions, key=lambda item: item.offset)
    for left, right in zip(ordered, ordered[1:]):
        if left.end > right.offset:
            raise ValueError(f"分区 {left.label!r} 与 {right.label!r} 存在重叠")

    for item in partitions:
        if item.label != "cardid" and item.offset < CARDID_OFFSET + CARDID_SIZE and CARDID_OFFSET < item.end:
            raise ValueError(f"分区 {item.label!r} 越界覆盖了受保护的 cardid 区域")

    app_path = build_dir / "FoloToy-AI-Passport.bin"
    if app_path.is_file():
        app_size = app_path.stat().st_size
        if app_size > APP_MAX_SIZE:
            raise ValueError(f"应用程序固件体积为 {app_size} 字节，超过了 3MB (APP_MAX_SIZE={APP_MAX_SIZE}) 限制！")
        print(f"[OK] 受保护固件布局验证通过: app 大小 {app_size} / {APP_MAX_SIZE} 字节 (<= 3MB)")

    if len(merged) <= 0x10000 or merged[0x10000] != 0xE9:
        raise ValueError("合并镜像在 0x10000 处缺少合法的 ESP 应用程序图像魔数 0xE9")

    # 验证 cardid 区域是否为全 0xFF 填充（未烧写状态）
    payload = merged[
        CARDID_OFFSET : min(len(merged), CARDID_OFFSET + CARDID_SIZE)
    ]
    if any(byte != 0xFF for byte in payload):
        raise ValueError("合并固件中包含了非法的 cardid 敏感数据字节")


def main() -> int:
    build_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "firmware/build").resolve()
    merged_path = build_dir / "FoloToy-AI-Passport-full.bin"
    flash_args_path = build_dir / "flash_args"

    if not merged_path.is_file():
        print(f"错误: 未找到合并固件 {merged_path}", file=sys.stderr)
        return 1

    merged = merged_path.read_bytes()
    for offset, relative_name in EXPECTED_IMAGES:
        image_path = build_dir / relative_name
        if not image_path.is_file():
            print(f"警告: 子固件缺失 {image_path}", file=sys.stderr)
            continue
        image = image_path.read_bytes()
        if merged[offset : offset + len(image)] != image:
            print(f"错误: {relative_name} 在合并偏移 0x{offset:x} 处不一致", file=sys.stderr)
            return 1
        print(f"[OK] 验证 {relative_name}: {len(image)} 字节 @ 0x{offset:x}")

    if len(merged) > FLASH_SIZE:
        print(f"错误: 合并固件总大小 ({len(merged)} 字节) 超过 8MB 限制", file=sys.stderr)
        return 1

    try:
        verify_protected_layout(merged, build_dir)
    except Exception as error:
        print(f"错误: {error}", file=sys.stderr)
        return 1

    print(f"[OK] 全量固件审计通过: {len(merged)} 字节，烧录起始地址 0x0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
