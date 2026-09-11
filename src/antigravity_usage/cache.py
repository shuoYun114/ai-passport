"""Antigravity 额度本地缓存与窗口自动推算推进模块。

当 Antigravity IDE 退出或未启动时，通过历史快照与到期推算提供无缝的额度预估，
同时确保只有成功的快照才会被持久化。
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
import time
from typing import Any, Dict, Optional

from .client import (
    ModelQuota,
    UsageSnapshot,
    UserAccount,
    normalize_user_status,
    parse_iso8601_to_timestamp,
)

logger = logging.getLogger(__name__)

DEFAULT_CACHE_PATH = (
    Path(os.environ.get("GEMINI_HOME", Path.home() / ".gemini"))
    / "antigravity-usage-cache.json"
)

# 默认滚动周期（当无法精确推断时，针对 Gemini/Claude 短期滚动额度默认为 5 小时 = 18000 秒）
DEFAULT_ROLLING_WINDOW_SECONDS = 5 * 3600


def save_cached_snapshot(
    snapshot: UsageSnapshot, path: Path = DEFAULT_CACHE_PATH
) -> None:
    """原子化持久化成功获取的用量快照。"""
    if not snapshot.available:
        return
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temp_path = path.with_suffix(".tmp")
        data = snapshot.to_dict()
        temp_path.write_text(
            json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        temp_path.replace(path)
        logger.debug("已成功保存额度快照至 %s", path)
    except OSError as exc:
        logger.warning("保存额度快照缓存失败: %s", exc)


def load_cached_snapshot(path: Path = DEFAULT_CACHE_PATH) -> Optional[UsageSnapshot]:
    """从本地文件加载上次保存的用量快照。"""
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or not data.get("available"):
            return None

        account_data = data.get("account", {})
        account = UserAccount(
            name=account_data.get("name", "Antigravity User"),
            email=account_data.get("email", ""),
            plan_name=account_data.get("plan_name", "Standard"),
            teams_tier=account_data.get("teams_tier", "TEAMS_TIER_UNKNOWN"),
            user_tier_name=account_data.get("user_tier_name", "Free Tier"),
            available_credits=account_data.get("available_credits", []),
        )

        models: list[ModelQuota] = []
        for m in data.get("models", []):
            reset_time_iso = m.get("reset_time")
            models.append(
                ModelQuota(
                    label=m.get("label", "Unknown Model"),
                    model_id=m.get("model_id", ""),
                    family=m.get("family", "Other"),
                    remaining_fraction=float(m.get("remaining_percent", 100.0)) / 100.0,
                    reset_time_iso=reset_time_iso,
                    reset_timestamp=parse_iso8601_to_timestamp(reset_time_iso),
                )
            )

        snapshot = UsageSnapshot(
            account=account,
            models=models,
            timestamp=data.get("timestamp", int(time.time())),
            available=True,
        )
        return snapshot
    except Exception as exc:
        logger.warning("加载额度快照缓存异常: %s", exc)
        return None


def roll_expired_quotas(
    snapshot: UsageSnapshot, now: Optional[int] = None
) -> bool:
    """若当前时间已超过配额重置时间点，自动将已到期模型的剩余额度推进恢复为 100%。

    Returns:
        bool: 是否有任何模型的额度被重置推进。
    """
    current_time = int(time.time()) if now is None else now
    changed = False

    for model in snapshot.models:
        if not model.reset_timestamp:
            continue
        if model.reset_timestamp <= current_time and model.remaining_fraction < 1.0:
            # 已经到期，额度已重置
            model.remaining_fraction = 1.0
            # 推进下一个重置周期
            elapsed = current_time - model.reset_timestamp
            cycles = elapsed // DEFAULT_ROLLING_WINDOW_SECONDS + 1
            model.reset_timestamp += cycles * DEFAULT_ROLLING_WINDOW_SECONDS
            changed = True

    if changed:
        snapshot.timestamp = current_time
    return changed
