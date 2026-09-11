"""Antigravity 活跃会话与任务监控模块。

用于实时追踪当前是否正在执行任务、活跃任务数量以及任务完成事件，
为命令行交互和 AI Passport 屏幕动效（“助手正在工作” / “任务已完成”）提供支持。
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
import time
from typing import Optional, Set

logger = logging.getLogger(__name__)

ACTIVE_LOOKBACK_SECONDS = 30  # 30秒内有写入被视为正在活跃执行


class AntigravitySessionWatcher:
    """监听 Antigravity 本地会话活跃度与任务状态。"""

    def __init__(self, antigravity_home: Optional[Path] = None) -> None:
        if antigravity_home is None:
            antigravity_home = (
                Path(os.environ.get("GEMINI_HOME", Path.home() / ".gemini"))
                / "antigravity"
            )
        self.home = antigravity_home
        self.conversations_dir = self.home / "conversations"
        self.brain_dir = self.home / "brain"
        self.completion_sequence = 0
        self._last_active_sessions: Set[str] = set()

    def get_active_sessions(self) -> Set[str]:
        """获取当前活跃（最近有写入动作）的会话 ID 集合。"""
        active: Set[str] = set()
        now = time.time()

        # 检查 conversations 目录下的 wal 文件
        if self.conversations_dir.exists():
            try:
                for wal_file in self.conversations_dir.glob("*.db-wal"):
                    try:
                        stat = wal_file.stat()
                        if now - stat.st_mtime <= ACTIVE_LOOKBACK_SECONDS and stat.st_size > 0:
                            session_id = wal_file.name.replace(".db-wal", "")
                            active.add(session_id)
                    except OSError:
                        continue
            except OSError:
                pass

        # 检查 brain 目录下的最近日志文件
        if self.brain_dir.exists():
            try:
                for conv_dir in self.brain_dir.iterdir():
                    if conv_dir.is_dir():
                        log_file = conv_dir / ".system_generated" / "logs" / "transcript.jsonl"
                        if log_file.exists():
                            try:
                                stat = log_file.stat()
                                if now - stat.st_mtime <= ACTIVE_LOOKBACK_SECONDS:
                                    active.add(conv_dir.name)
                            except OSError:
                                continue
            except OSError:
                pass

        return active

    @property
    def running_task_count(self) -> int:
        """当前正在进行的任务/活跃会话数量。"""
        return len(self.get_active_sessions())

    @property
    def is_running(self) -> bool:
        """是否有任务正在运行。"""
        return self.running_task_count > 0

    def poll(self) -> bool:
        """轮询会话状态。

        Returns:
            bool: 状态是否有变化（例如从运行变为空闲，或有新任务开始）。
        """
        current_active = self.get_active_sessions()
        changed = False

        # 如果先前处于活跃状态，而现在某些会话完成了，递增完成序列号
        completed_sessions = self._last_active_sessions - current_active
        if completed_sessions:
            self.completion_sequence += len(completed_sessions)
            changed = True

        if current_active != self._last_active_sessions:
            changed = True

        self._last_active_sessions = current_active
        return changed
