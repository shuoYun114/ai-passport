"""Antigravity Language Server 进程与服务发现模块。

负责自动探测宿主机上运行的 Antigravity 语言服务进程，
自动提取 CSRF 认证令牌并识别本地监听端口。
"""

from __future__ import annotations

import dataclasses
import json
import logging
import re
import socket
import ssl
import subprocess
import sys
import urllib.error
import urllib.request
from typing import Any, List, Optional

logger = logging.getLogger(__name__)


@dataclasses.dataclass
class ServerEndpoint:
    """Antigravity Language Server 服务端点信息。"""

    port: int
    csrf_token: str
    protocol: str = "http"  # 'http' 或 'https'
    pid: Optional[int] = None

    @property
    def base_url(self) -> str:
        return f"{self.protocol}://127.0.0.1:{self.port}"

    @property
    def get_user_status_url(self) -> str:
        return f"{self.base_url}/exa.language_server_pb.LanguageServerService/GetUserStatus"


def _extract_csrf_token(cmdline_str: str) -> Optional[str]:
    """从命令行字符串中提取 --csrf_token 参数值。"""
    match = re.search(r"--csrf_token(?:=|\s+)([a-zA-Z0-9-]+)", cmdline_str)
    if match:
        return match.group(1)
    return None


def _get_process_listen_ports(pid: int) -> List[int]:
    """获取指定进程 ID 当前正在监听的本地 TCP 端口列表。"""
    ports: List[int] = []

    # 优先使用 psutil
    try:
        import psutil

        proc = psutil.Process(pid)
        for conn in proc.net_connections(kind="tcp"):
            if conn.status == psutil.CONN_LISTEN and conn.laddr:
                ports.append(conn.laddr.port)
        if ports:
            return sorted(list(set(ports)))
    except Exception as exc:
        logger.debug("通过 psutil 获取进程 %s 端口失败: %s", pid, exc)

    # 在 Windows 下如果 psutil 获取为空，回退使用 netstat 解析
    if sys.platform == "win32":
        try:
            output = subprocess.check_output(
                ["netstat", "-ano", "-p", "tcp"], text=True, stderr=subprocess.DEVNULL
            )
            for line in output.splitlines():
                parts = line.strip().split()
                # 示例: TCP 127.0.0.1:9168 0.0.0.0:0 LISTENING 20800
                if len(parts) >= 5 and parts[3].upper() == "LISTENING":
                    line_pid = parts[4]
                    if line_pid == str(pid):
                        addr = parts[1]
                        if ":" in addr:
                            port_str = addr.rsplit(":", 1)[1]
                            if port_str.isdigit():
                                ports.append(int(port_str))
        except Exception as exc:
            logger.debug("通过 netstat 解析进程端口失败: %s", exc)

    return sorted(list(set(ports)))


def probe_endpoint(
    port: int, csrf_token: str, timeout_seconds: float = 2.0
) -> Optional[str]:
    """对指定端口进行健康探测，返回可用的协议 ('http' 或 'https')，若不可用返回 None。"""
    ssl_context = ssl.create_default_context()
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

    for protocol in ("http", "https"):
        url = f"{protocol}://127.0.0.1:{port}/exa.language_server_pb.LanguageServerService/GetUserStatus"
        headers = {
            "Content-Type": "application/json",
            "x-codeium-csrf-token": csrf_token,
        }
        req = urllib.request.Request(url, data=b"{}", headers=headers, method="POST")
        try:
            ctx = ssl_context if protocol == "https" else None
            with urllib.request.urlopen(req, context=ctx, timeout=timeout_seconds) as resp:
                if resp.status == 200:
                    return protocol
        except Exception:
            continue

    return None


def discover_server_endpoint(
    manual_port: Optional[int] = None, manual_token: Optional[str] = None
) -> Optional[ServerEndpoint]:
    """自动发现或使用指定参数构建 Antigravity Language Server 服务端点。

    Args:
        manual_port: 用户手动指定的端口（若提供则优先使用）。
        manual_token: 用户手动指定的 CSRF Token（若提供则优先使用）。

    Returns:
        成功探测到的 ServerEndpoint，若未找到则返回 None。
    """
    if manual_port and manual_token:
        proto = probe_endpoint(manual_port, manual_token) or "http"
        return ServerEndpoint(port=manual_port, csrf_token=manual_token, protocol=proto)

    import psutil

    # 扫描所有进程
    candidates: List[tuple[int, str, List[int]]] = []
    for proc in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            name = (proc.info["name"] or "").lower()
            cmdline_list = proc.info["cmdline"] or []
            cmdline_str = " ".join(cmdline_list)

            # 匹配 language_server 进程或携带 --csrf_token 的 antigravity 进程
            is_match = "language_server" in name or "--csrf_token" in cmdline_str
            if not is_match:
                continue

            token = manual_token or _extract_csrf_token(cmdline_str)
            if not token:
                continue

            ports = _get_process_listen_ports(proc.pid)
            if manual_port and manual_port not in ports:
                ports.append(manual_port)

            candidates.append((proc.pid, token, ports))
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

    # 对候选进程与端口进行验证
    for pid, token, ports in candidates:
        for port in ports:
            proto = probe_endpoint(port, token)
            if proto:
                logger.info("成功连接到 Language Server (PID: %s, 端口: %s, 协议: %s)", pid, port, proto)
                return ServerEndpoint(port=port, csrf_token=token, protocol=proto, pid=pid)

    return None
