"""测试 FoloToy AI Passport 报文载荷生成与兼容性。"""

import json
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src")))

from antigravity_usage.bridge import build_ai_passport_payload
from antigravity_usage.client import ModelQuota, UsageSnapshot, UserAccount
from antigravity_usage.watcher import AntigravitySessionWatcher


class TestBridge(unittest.TestCase):
    def test_build_ai_passport_payload_ready(self):
        account = UserAccount(
            name="Charlie",
            email="charlie@example.com",
            plan_name="Pro",
            teams_tier="TEAMS_TIER_PRO",
            user_tier_name="Google AI Pro",
            available_credits=[],
        )
        models = [
            ModelQuota(
                label="Gemini 3.8 Flash (High)",
                model_id="gemini-3.8-flash-high",
                family="Gemini",
                remaining_fraction=0.8,  # 80% 剩余，20% 已用
                reset_time_iso="2026-09-11T12:00:00Z",
                reset_timestamp=1789110000,
            ),
            ModelQuota(
                label="Claude Opus 4.6 (Thinking)",
                model_id="claude-opus-4-6-thinking",
                family="Claude",
                remaining_fraction=0.5,  # 50% 剩余，50% 已用
                reset_time_iso="2026-09-11T15:00:00Z",
                reset_timestamp=1789120000,
            ),
        ]
        snap = UsageSnapshot(account=account, models=models, available=True)
        watcher = AntigravitySessionWatcher(antigravity_home=Path("/non_existent_empty_dir"))

        # 空闲就绪状态
        raw_bytes = build_ai_passport_payload(snap, watcher, completed=False)
        self.assertTrue(raw_bytes.endswith(b"\n"))

        data = json.loads(raw_bytes.decode("utf-8"))
        self.assertEqual(data["msg"], "助手已就绪")
        self.assertIn("codex", data)
        self.assertEqual(data["codex"]["primary_used"], 20)
        self.assertEqual(data["codex"]["secondary_used"], 50)
        self.assertEqual(data["codex"]["plan"], "Pro")

        # 验证扩展的 antigravity 节点
        self.assertIn("antigravity", data)
        self.assertEqual(data["antigravity"]["user"], "Charlie")
        self.assertEqual(data["antigravity"]["tier"], "Google AI Pro")
        self.assertEqual(data["antigravity"]["primary_remaining"], 80.0)

    def test_build_ai_passport_payload_completed(self):
        account = UserAccount(
            name="Charlie",
            email="charlie@example.com",
            plan_name="Pro",
            teams_tier="TEAMS_TIER_PRO",
            user_tier_name="Google AI Pro",
            available_credits=[],
        )
        snap = UsageSnapshot(account=account, models=[], available=True)
        watcher = AntigravitySessionWatcher(antigravity_home=Path("/non_existent_empty_dir"))

        raw_bytes = build_ai_passport_payload(snap, watcher, completed=True)
        data = json.loads(raw_bytes.decode("utf-8"))
        self.assertEqual(data["msg"], "任务已完成")


if __name__ == "__main__":
    unittest.main()
