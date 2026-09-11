"""Antigravity 额度控制台富文本与进度条格式化器。"""

from __future__ import annotations

import sys
from typing import List, Optional

from .client import ModelQuota, UsageSnapshot

# ANSI 颜色码
COLOR_RESET = "\033[0m"
COLOR_BOLD = "\033[1m"
COLOR_GREEN = "\033[32m"
COLOR_YELLOW = "\033[33m"
COLOR_RED = "\033[31m"
COLOR_CYAN = "\033[36m"
COLOR_BLUE = "\033[34m"
COLOR_MAGENTA = "\033[35m"
COLOR_GRAY = "\033[90m"


def _get_bar_chars() -> tuple[str, str]:
    """检测当前标准输出编码是否支持特殊方块字符，返回 (filled_char, empty_char)。"""
    encoding = getattr(sys.stdout, "encoding", "") or "utf-8"
    try:
        "█░".encode(encoding)
        return ("█", "░")
    except UnicodeEncodeError:
        return ("#", "-")


def _make_progress_bar(percent: float, width: int = 20) -> str:
    """生成漂亮的进度条字符，自动适配终端字符集。"""
    filled_char, empty_char = _get_bar_chars()
    filled_len = int(round(width * percent / 100.0))
    filled_len = max(0, min(width, filled_len))
    empty_len = width - filled_len

    if percent >= 50.0:
        color = COLOR_GREEN
    elif percent >= 20.0:
        color = COLOR_YELLOW
    else:
        color = COLOR_RED

    bar = f"{color}{filled_char * filled_len}{empty_char * empty_len}{COLOR_RESET}"
    return bar


def format_console_dashboard(
    snapshot: UsageSnapshot,
    running_task_count: int = 0,
    filter_keyword: Optional[str] = None,
) -> str:
    """构建控制台仪表盘展示文本。"""
    lines: List[str] = []

    # 标题与顶栏
    lines.append(f"{COLOR_BOLD}{COLOR_CYAN}======================================================================{COLOR_RESET}")
    lines.append(f"{COLOR_BOLD}{COLOR_CYAN}               Google Antigravity 额度与用量监控看板                  {COLOR_RESET}")
    lines.append(f"{COLOR_BOLD}{COLOR_CYAN}======================================================================{COLOR_RESET}")

    # 用户与账号状态卡片
    acc = snapshot.account
    status_text = (
        f"{COLOR_GREEN}[工作中] ({running_task_count} 个活跃任务){COLOR_RESET}"
        if running_task_count > 0
        else f"{COLOR_BLUE}[已就绪] (空闲){COLOR_RESET}"
    )

    lines.append(f" {COLOR_BOLD}用户账户:{COLOR_RESET} {acc.name} ({acc.email or '未绑定邮箱'})")
    lines.append(f" {COLOR_BOLD}订阅层级:{COLOR_RESET} {COLOR_MAGENTA}{acc.user_tier_name} ({acc.plan_name}){COLOR_RESET}   |   {COLOR_BOLD}状态:{COLOR_RESET} {status_text}")
    lines.append(f"{COLOR_GRAY}----------------------------------------------------------------------{COLOR_RESET}")

    # 主力模型概览
    if snapshot.primary_model:
        pm = snapshot.primary_model
        lines.append(
            f" {COLOR_BOLD}主力模型:{COLOR_RESET} {pm.label:<26} "
            f"{_make_progress_bar(pm.remaining_percent, 18)} "
            f"剩余: {COLOR_BOLD}{pm.remaining_percent:5.1f}%{COLOR_RESET}  (重置: {pm.reset_countdown})"
        )
    if snapshot.secondary_model and snapshot.secondary_model != snapshot.primary_model:
        sm = snapshot.secondary_model
        lines.append(
            f" {COLOR_BOLD}辅助模型:{COLOR_RESET} {sm.label:<26} "
            f"{_make_progress_bar(sm.remaining_percent, 18)} "
            f"剩余: {COLOR_BOLD}{sm.remaining_percent:5.1f}%{COLOR_RESET}  (重置: {sm.reset_countdown})"
        )

    lines.append(f"{COLOR_GRAY}----------------------------------------------------------------------{COLOR_RESET}")
    lines.append(
        f" {COLOR_BOLD}{'模型名称':<28} {'额度进度':<24} {'剩余比例':<12} {'重置倒计时'}{COLOR_RESET}"
    )
    lines.append(f"{COLOR_GRAY}----------------------------------------------------------------------{COLOR_RESET}")

    # 过滤与分类展示
    models = snapshot.models
    if filter_keyword:
        kw = filter_keyword.lower()
        models = [m for m in models if kw in m.label.lower() or kw in m.model_id.lower()]

    # 按模型家族分组显示
    current_family = None
    for m in models:
        if m.family != current_family:
            current_family = m.family
            lines.append(f"{COLOR_BOLD}{COLOR_BLUE}>> {current_family} 系列模型{COLOR_RESET}")

        bar = _make_progress_bar(m.remaining_percent, 16)
        percent_str = f"{m.remaining_percent:5.1f}%"
        if m.remaining_percent >= 50.0:
            percent_colored = f"{COLOR_GREEN}{percent_str}{COLOR_RESET}"
        elif m.remaining_percent >= 20.0:
            percent_colored = f"{COLOR_YELLOW}{percent_str}{COLOR_RESET}"
        else:
            percent_colored = f"{COLOR_RED}{percent_str}{COLOR_RESET}"

        reset_str = m.reset_countdown
        lines.append(f"  {m.label:<27} {bar}  {percent_colored}   {COLOR_GRAY}{reset_str}{COLOR_RESET}")

    lines.append(f"{COLOR_BOLD}{COLOR_CYAN}======================================================================{COLOR_RESET}")
    return "\n".join(lines)
