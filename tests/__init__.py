"""测试套件初始化，自动将 src 目录添加至 sys.path。"""

import os
import sys

src_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src"))
if src_path not in sys.path:
    sys.path.insert(0, src_path)
