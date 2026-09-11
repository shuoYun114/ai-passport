import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

c_font = (ROOT / "firmware" / "main" / "buddy_font_zh_14.c").read_text(encoding="utf-8")
ui_code = (ROOT / "firmware" / "main" / "buddy_ui.c").read_text(encoding="utf-8")

# 提取代码中所有的字符串字面量
strs = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', ui_code)
chinese_in_strings = set("".join(re.findall(r"[\u4e00-\u9fff]", "".join(strs))))

missing = [c for c in chinese_in_strings if f"U+{ord(c):04X}" not in c_font]
print("屏幕字符串中中文字符总数:", len(chinese_in_strings))
print("屏幕字符串中缺失字数:", len(missing))
if missing:
    print("缺失的文字:", missing)
else:
    print("[SUCCESS] 屏幕上所有的汉字 100% 完整收录在字库中，彻底无缺字乱码！")
