"""测试本地快照缓存与到期滚动推算。"""

import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src")))

from antigravity_usage.cache import (
    load_cached_snapshot,
    roll_expired_quotas,
    save_cached_snapshot,
)
from antigravity_usage.client import ModelQuota, UsageSnapshot, UserAccount


class TestCache(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.cache_file = Path(self.temp_dir.name) / "test-cache.json"

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_save_and_load_cache(self):
        account = UserAccount(
            name="Alice",
            email="alice@example.com",
            plan_name="Ultra",
            teams_tier="TEAMS_TIER_PRO",
            user_tier_name="Google AI Ultra",
            available_credits=[],
        )
        models = [
            ModelQuota(
                label="Gemini 3.8 Flash (High)",
                model_id="gemini-3.8-flash-high",
                family="Gemini",
                remaining_fraction=0.6,
                reset_time_iso="2026-09-11T10:00:00Z",
                reset_timestamp=1789110000,
            )
        ]
        snap = UsageSnapshot(account=account, models=models, available=True)

        save_cached_snapshot(snap, self.cache_file)
        self.assertTrue(self.cache_file.exists())

        loaded = load_cached_snapshot(self.cache_file)
        self.assertIsNotNone(loaded)
        self.assertEqual(loaded.account.name, "Alice")
        self.assertEqual(loaded.account.plan_name, "Ultra")
        self.assertEqual(len(loaded.models), 1)
        self.assertAlmostEqual(loaded.models[0].remaining_percent, 60.0)

    def test_roll_expired_quotas(self):
        account = UserAccount(
            name="Bob",
            email="bob@example.com",
            plan_name="Pro",
            teams_tier="TEAMS_TIER_PRO",
            user_tier_name="Google AI Pro",
            available_credits=[],
        )
        now = 20000
        # 已过期的模型 (reset_timestamp = 10000 <= now)
        models = [
            ModelQuota(
                label="Gemini 3.8 Flash",
                model_id="gemini-3.8-flash",
                family="Gemini",
                remaining_fraction=0.3,  # 剩余 30%
                reset_time_iso="2026-01-01T00:00:00Z",
                reset_timestamp=10000,
            )
        ]
        snap = UsageSnapshot(account=account, models=models, available=True)

        changed = roll_expired_quotas(snap, now=now)
        self.assertTrue(changed)
        # 到期后自动恢复为 100%
        self.assertEqual(snap.models[0].remaining_fraction, 1.0)
        self.assertEqual(snap.models[0].remaining_percent, 100.0)
        # 重置时间戳被向后推进
        self.assertGreater(snap.models[0].reset_timestamp, now)


if __name__ == "__main__":
    unittest.main()
