"""测试 Token Monitor 跨工具采集引擎。"""

import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools")))

from token_monitor_collector import (
    AntigravityCollector,
    ClaudeCodeCollector,
    CodexCollector,
    CursorCollector,
    ToolUsage,
    collect_all_tools,
)


class TestCollector(unittest.TestCase):
    def test_collect_all_tools(self):
        snap = collect_all_tools()
        self.assertTrue(snap.available)
        self.assertGreaterEqual(len(snap.tools), 1)
        self.assertGreaterEqual(snap.tokens_today, 0)
        self.assertGreaterEqual(snap.cost_today_cents, 0)
        self.assertEqual(snap.currency, "USD")

    def test_tool_usage_structure(self):
        tool = ToolUsage(
            name="TestAI",
            tokens_today=50000,
            cost_cents=75,
            remaining_percent=80.0,
            used_percent=20,
        )
        self.assertEqual(tool.name, "TestAI")
        self.assertEqual(tool.tokens_today, 50000)
        self.assertEqual(tool.cost_cents, 75)
        self.assertEqual(tool.remaining_percent, 80.0)


if __name__ == "__main__":
    unittest.main()
