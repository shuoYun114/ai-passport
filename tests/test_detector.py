"""测试服务发现与参数提取逻辑。"""

import os
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src")))

from antigravity_usage.detector import ServerEndpoint, _extract_csrf_token


class TestDetector(unittest.TestCase):
    def test_extract_csrf_token(self):
        cmd1 = "language_server.exe --standalone --csrf_token 60ad087c-5e6f-4f74-a887-76205a5092f9 --port 0"
        self.assertEqual(
            _extract_csrf_token(cmd1), "60ad087c-5e6f-4f74-a887-76205a5092f9"
        )

        cmd2 = "language_server --csrf_token=abcd-1234-efgh"
        self.assertEqual(_extract_csrf_token(cmd2), "abcd-1234-efgh")

        cmd3 = "antigravity.exe"
        self.assertIsNone(_extract_csrf_token(cmd3))

    def test_server_endpoint_urls(self):
        endpoint = ServerEndpoint(port=9167, csrf_token="token-xyz", protocol="https")
        self.assertEqual(endpoint.base_url, "https://127.0.0.1:9167")
        self.assertEqual(
            endpoint.get_user_status_url,
            "https://127.0.0.1:9167/exa.language_server_pb.LanguageServerService/GetUserStatus",
        )


if __name__ == "__main__":
    unittest.main()
