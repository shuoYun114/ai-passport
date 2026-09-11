"""Antigravity Connect RPC 客户端与配额归一化解析器。"""

from __future__ import annotations

import dataclasses
from datetime import datetime, timezone
import json
import logging
import ssl
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

from .detector import ServerEndpoint, discover_server_endpoint

logger = logging.getLogger(__name__)


def parse_iso8601_to_timestamp(iso_str: Optional[str]) -> Optional[int]:
    """将 ISO 8601 时间字符串解析为 Unix 秒级时间戳。"""
    if not iso_str:
        return None
    try:
        clean_str = iso_str.replace("Z", "+00:00")
        dt = datetime.fromisoformat(clean_str)
        return int(dt.timestamp())
    except Exception as exc:
        logger.debug("解析时间戳失败 (%s): %s", iso_str, exc)
        return None


def format_duration(seconds: int) -> str:
    """将剩余秒数转换为易读的中文倒计时描述。"""
    if seconds <= 0:
        return "即将重置"
    hours = seconds // 3600
    minutes = (seconds % 3600) // 60
    secs = seconds % 60
    if hours > 24:
        days = hours // 24
        rem_hours = hours % 24
        return f"{days}天{rem_hours}小时"
    if hours > 0:
        return f"{hours}小时{minutes}分"
    if minutes > 0:
        return f"{minutes}分{secs}秒"
    return f"{secs}秒"


@dataclasses.dataclass
class ModelQuota:
    """单个模型的配额用量信息。"""

    label: str
    model_id: str
    family: str
    remaining_fraction: float  # 0.0 ~ 1.0
    reset_time_iso: Optional[str]
    reset_timestamp: Optional[int]

    @property
    def remaining_percent(self) -> float:
        """剩余配额百分比 (0.0 ~ 100.0)。"""
        return round(max(0.0, min(100.0, self.remaining_fraction * 100.0)), 2)

    @property
    def used_percent(self) -> float:
        """已用配额百分比 (0.0 ~ 100.0)。"""
        return round(max(0.0, min(100.0, 100.0 - self.remaining_percent)), 2)

    @property
    def seconds_until_reset(self) -> int:
        """距离配额重置的剩余秒数。"""
        if not self.reset_timestamp:
            return 0
        now = int(time.time())
        return max(0, self.reset_timestamp - now)

    @property
    def reset_countdown(self) -> str:
        """人类易读的重置倒计时。"""
        return format_duration(self.seconds_until_reset)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "label": self.label,
            "model_id": self.model_id,
            "family": self.family,
            "remaining_percent": self.remaining_percent,
            "used_percent": self.used_percent,
            "reset_time": self.reset_time_iso,
            "seconds_until_reset": self.seconds_until_reset,
            "reset_countdown": self.reset_countdown,
        }


@dataclasses.dataclass
class UserAccount:
    """用户信息与订阅权益。"""

    name: str
    email: str
    plan_name: str
    teams_tier: str
    user_tier_name: str
    available_credits: List[Dict[str, Any]]

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "email": self.email,
            "plan_name": self.plan_name,
            "teams_tier": self.teams_tier,
            "user_tier_name": self.user_tier_name,
            "available_credits": self.available_credits,
        }


@dataclasses.dataclass
class UsageSnapshot:
    """全量用量快照数据。"""

    account: UserAccount
    models: List[ModelQuota]
    timestamp: int = dataclasses.field(default_factory=lambda: int(time.time()))
    available: bool = True

    @property
    def primary_model(self) -> Optional[ModelQuota]:
        """首选主力模型（默认优先取 Gemini 3.8 Flash High 或列表第一个）。"""
        for m in self.models:
            if "3.8 Flash (High)" in m.label:
                return m
        for m in self.models:
            if "Flash" in m.label:
                return m
        return self.models[0] if self.models else None

    @property
    def secondary_model(self) -> Optional[ModelQuota]:
        """次选主力模型（默认优先取 Claude Opus/Sonnet 或 Gemini 3.1 Pro）。"""
        for m in self.models:
            if "Claude" in m.label:
                return m
        for m in self.models:
            if "3.1 Pro" in m.label:
                return m
        return self.models[1] if len(self.models) > 1 else None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "timestamp": self.timestamp,
            "available": self.available,
            "account": self.account.to_dict(),
            "primary": self.primary_model.to_dict() if self.primary_model else None,
            "secondary": self.secondary_model.to_dict() if self.secondary_model else None,
            "models": [m.to_dict() for m in self.models],
        }


def _infer_model_family(label: str, model_id: str) -> str:
    """根据模型标签或 ID 推导模型所属家族。"""
    combined = f"{label} {model_id}".lower()
    if "gemini" in combined:
        return "Gemini"
    if "claude" in combined:
        return "Claude"
    if "gpt" in combined or "openai" in combined:
        return "GPT"
    return "Other"


def normalize_user_status(raw_data: Dict[str, Any]) -> UsageSnapshot:
    """将 LanguageServerService.GetUserStatus 的原始响应标准化为 UsageSnapshot。"""
    user_status = raw_data.get("userStatus", {})
    if not isinstance(user_status, dict):
        user_status = {}

    plan_info = user_status.get("planStatus", {}).get("planInfo", {})
    user_tier = user_status.get("userTier", {})

    account = UserAccount(
        name=str(user_status.get("name") or "Antigravity User"),
        email=str(user_status.get("email") or ""),
        plan_name=str(plan_info.get("planName") or "Standard"),
        teams_tier=str(plan_info.get("teamsTier") or "TEAMS_TIER_UNKNOWN"),
        user_tier_name=str(user_tier.get("name") or "Free Tier"),
        available_credits=user_tier.get("availableCredits") or [],
    )

    models: List[ModelQuota] = []
    configs = (
        user_status.get("cascadeModelConfigData", {}).get("clientModelConfigs")
        or user_status.get("cascadeModelConfig", {}).get("clientModelConfigs")
        or []
    )

    for cfg in configs:
        if not isinstance(cfg, dict):
            continue
        label = str(cfg.get("label") or "Unknown Model")
        model_id = str(cfg.get("modelId") or "")
        quota_info = cfg.get("quotaInfo", {})

        rem_frac = 1.0
        reset_time_iso = None
        reset_ts = None

        if isinstance(quota_info, dict):
            if "remainingFraction" in quota_info:
                try:
                    rem_frac = float(quota_info["remainingFraction"])
                except (ValueError, TypeError):
                    rem_frac = 1.0
            reset_time_iso = quota_info.get("resetTime")
            reset_ts = parse_iso8601_to_timestamp(reset_time_iso)

        family = _infer_model_family(label, model_id)
        models.append(
            ModelQuota(
                label=label,
                model_id=model_id,
                family=family,
                remaining_fraction=rem_frac,
                reset_time_iso=reset_time_iso,
                reset_timestamp=reset_ts,
            )
        )

    return UsageSnapshot(account=account, models=models, available=True)


class AntigravityClient:
    """Antigravity 额度查询客户端。"""

    def __init__(self, endpoint: Optional[ServerEndpoint] = None) -> None:
        self.endpoint = endpoint
        self._ssl_context = ssl.create_default_context()
        self._ssl_context.check_hostname = False
        self._ssl_context.verify_mode = ssl.CERT_NONE

    def ensure_endpoint(self) -> ServerEndpoint:
        """确保服务端点可用，若未指定则自动发现。"""
        if self.endpoint is None:
            discovered = discover_server_endpoint()
            if discovered is None:
                raise RuntimeError(
                    "未检测到运行中的 Antigravity Language Server 进程，"
                    "请确认 Antigravity IDE 是否已启动。"
                )
            self.endpoint = discovered
        return self.endpoint

    def get_user_status_raw(self, timeout_seconds: float = 5.0) -> Dict[str, Any]:
        """向服务端发起 GetUserStatus RPC 请求，返回原始 JSON 数据字典。"""
        endpoint = self.ensure_endpoint()
        headers = {
            "Content-Type": "application/json",
            "x-codeium-csrf-token": endpoint.csrf_token,
        }
        req = urllib.request.Request(
            endpoint.get_user_status_url,
            data=b"{}",
            headers=headers,
            method="POST",
        )
        ctx = self._ssl_context if endpoint.protocol == "https" else None
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=timeout_seconds) as resp:
                body = resp.read().decode("utf-8")
                return json.loads(body)
        except urllib.error.URLError as err:
            # 如果原先探到的端点失效，重置端点并抛出异常
            self.endpoint = None
            raise RuntimeError(f"请求 Antigravity 额度服务失败: {err}") from err

    def fetch_usage_snapshot(self, timeout_seconds: float = 5.0) -> UsageSnapshot:
        """获取并解析当前最新的额度快照。"""
        raw = self.get_user_status_raw(timeout_seconds=timeout_seconds)
        return normalize_user_status(raw)
