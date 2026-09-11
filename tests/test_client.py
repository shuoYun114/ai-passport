"""测试客户端数据模型与归一化解析。"""

import os
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src")))

from antigravity_usage.client import (
    format_duration,
    normalize_user_status,
    parse_iso8601_to_timestamp,
)


class TestClient(unittest.TestCase):
    def test_format_duration(self):
        self.assertEqual(format_duration(0), "即将重置")
        self.assertEqual(format_duration(45), "45秒")
        self.assertEqual(format_duration(125), "2分5秒")
        self.assertEqual(format_duration(3660), "1小时1分")
        self.assertEqual(format_duration(86400 + 7200), "1天2小时")

    def test_parse_iso8601(self):
        ts = parse_iso8601_to_timestamp("2026-09-11T06:57:44Z")
        self.assertIsNotNone(ts)
        self.assertGreater(ts, 1700000000)

    def test_normalize_user_status(self):
        mock_raw = {
            "userStatus": {
                "name": "Test User",
                "email": "test@example.com",
                "planStatus": {
                    "planInfo": {"planName": "Pro", "teamsTier": "TEAMS_TIER_PRO"}
                },
                "userTier": {"name": "Google AI Pro", "availableCredits": []},
                "cascadeModelConfigData": {
                    "clientModelConfigs": [
                        {
                            "label": "Gemini 3.8 Flash (High)",
                            "modelId": "gemini-3.8-flash-high",
                            "quotaInfo": {
                                "remainingFraction": 0.75,
                                "resetTime": "2026-09-11T12:00:00Z",
                            },
                        },
                        {
                            "label": "Claude Sonnet 4.6",
                            "modelId": "claude-sonnet",
                            "quotaInfo": {
                                "remainingFraction": 0.5,
                                "resetTime": "2026-09-11T15:00:00Z",
                            },
                        },
                    ]
                },
            }
        }

        snapshot = normalize_user_status(mock_raw)
        self.assertTrue(snapshot.available)
        self.assertEqual(snapshot.account.name, "Test User")
        self.assertEqual(snapshot.account.plan_name, "Pro")
        self.assertEqual(len(snapshot.models), 2)

        m1 = snapshot.models[0]
        self.assertEqual(m1.label, "Gemini 3.8 Flash (High)")
        self.assertEqual(m1.remaining_percent, 75.0)
        self.assertEqual(m1.used_percent, 25.0)
        self.assertEqual(m1.family, "Gemini")

        m2 = snapshot.models[1]
        self.assertEqual(m2.remaining_percent, 50.0)
        self.assertEqual(m2.used_percent, 50.0)
        self.assertEqual(m2.family, "Claude")

        # 主力模型与辅助模型推断
        self.assertIsNotNone(snapshot.primary_model)
        self.assertEqual(snapshot.primary_model.label, "Gemini 3.8 Flash (High)")
        self.assertIsNotNone(snapshot.secondary_model)
        self.assertEqual(snapshot.secondary_model.label, "Claude Sonnet 4.6")


if __name__ == "__main__":
    unittest.main()
