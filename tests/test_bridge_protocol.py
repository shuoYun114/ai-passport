"""测试 Token Monitor 硬件桥接协议报文生成。"""

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools")))

from token_monitor_bridge import build_firmware_payload
from token_monitor_collector import TokenMonitorSnapshot, ToolUsage


class TestBridgeProtocol(unittest.TestCase):
    def test_build_firmware_payload(self):
        tools = [
            ToolUsage(
                name="Antigravity",
                tokens_today=100000,
                cost_cents=150,
                used_percent=40,
                remaining_percent=60.0,
            ),
            ToolUsage(
                name="Claude",
                tokens_today=50000,
                cost_cents=80,
                used_percent=10,
                remaining_percent=90.0,
            ),
        ]
        snap = TokenMonitorSnapshot(
            tokens_today=150000,
            tokens_total=300000,
            cost_today_cents=230,
            cost_total_cents=460,
            currency="USD",
            running_tasks=2,
            tools=tools,
            primary_quota_label="Antigravity",
            primary_used_percent=40,
            primary_reset_timestamp=1789110000,
            secondary_quota_label="Claude",
            secondary_used_percent=10,
            secondary_reset_timestamp=1789120000,
            available=True,
            message="助手正在工作",
        )

        payload_bytes = build_firmware_payload(snap, completed=False)
        self.assertTrue(payload_bytes.endswith(b"\n"))

        data = json.loads(payload_bytes.decode("utf-8"))
        self.assertEqual(data["tokens_today"], 150000)
        self.assertEqual(data["total"], 2)
        self.assertEqual(data["running"], 2)
        self.assertEqual(data["msg"], "助手正在工作")

        # 检查 token_monitor 节点
        self.assertIn("token_monitor", data)
        tm = data["token_monitor"]
        self.assertEqual(tm["tokens_today"], 150000)
        self.assertEqual(tm["cost_today_cents"], 230)
        self.assertEqual(tm["currency"], "USD")
        self.assertEqual(len(tm["tools"]), 2)
        self.assertEqual(tm["tools"][0]["name"], "Antigravity")
        self.assertEqual(tm["tools"][0]["tokens_today"], 100000)

        # 检查向后兼容的 codex 节点
        self.assertIn("codex", data)
        codex = data["codex"]
        self.assertEqual(codex["primary_used"], 40)
        self.assertEqual(codex["secondary_used"], 10)


if __name__ == "__main__":
    unittest.main()
