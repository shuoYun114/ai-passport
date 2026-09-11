#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试分区表二进制生成与 ESP-IDF 规范合规性
"""

import tempfile
import unittest
from pathlib import Path
from tools.gen_partition_table import compile_csv_to_bin
from tools.verify_firmware import parse_partition_table


class TestGenPartition(unittest.TestCase):
    def test_compile_partitions(self):
        csv_content = (
            "# Name,   Type, SubType, Offset,  Size, Flags\n"
            "nvs,      data, nvs,     0x9000,  0x6000,\n"
            "factory,  app,  factory, 0x10000, 0x300000,\n"
            "cardid,   data, 0x02,    0x356000,0x4000,\n"
        )
        with tempfile.TemporaryDirectory() as tmpdir:
            csv_path = Path(tmpdir) / "test_parts.csv"
            bin_path = Path(tmpdir) / "test_parts.bin"
            csv_path.write_text(csv_content, encoding="utf-8")

            bin_data = compile_csv_to_bin(csv_path, bin_path)
            self.assertEqual(len(bin_data), 3072)
            self.assertTrue(bin_path.is_file())

            partitions, md5_ok = parse_partition_table(bin_data)
            self.assertTrue(md5_ok, "MD5 标记必须有效")
            self.assertEqual(len(partitions), 3)

            by_name = {p.label: p for p in partitions}
            self.assertIn("factory", by_name)
            self.assertEqual(by_name["factory"].offset, 0x10000)
            self.assertEqual(by_name["factory"].size, 0x300000)
            self.assertEqual(by_name["cardid"].offset, 0x356000)


if __name__ == "__main__":
    unittest.main()
