"""配置：环境变量优先，其次 config.toml，最后默认值。"""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover
    tomllib = None


@dataclass
class Config:
    # 小智官方云 MCP 接入点地址（账号后台获取），形如 wss://api.xiaozhi.me/mcp/?token=...
    xiaozhi_mcp_endpoint: str = ""
    # 固件 CcNotifyServer 的地址
    device_ip: str = ""
    device_port: int = 8930
    # 运行交互式 claude 的 tmux 会话名（tmux new -s cc claude）
    tmux_session: str = "cc"
    # 本地 hook 接收服务监听地址
    hook_host: str = "127.0.0.1"
    hook_port: int = 8788
    # PreToolUse 阻塞等待语音裁决的超时（秒），略小于 Claude Code 的 600s 上限
    permission_timeout: float = 590.0


def load(path: str | None = None) -> Config:
    cfg = Config()

    # 1) config.toml
    toml_path = Path(path) if path else Path(__file__).resolve().parent.parent / "config.toml"
    if tomllib is not None and toml_path.exists():
        with toml_path.open("rb") as f:
            data = tomllib.load(f)
        for key, value in data.items():
            if hasattr(cfg, key):
                setattr(cfg, key, value)

    # 2) 环境变量覆盖
    env_map = {
        "XIAOZHI_MCP_ENDPOINT": "xiaozhi_mcp_endpoint",
        "DEVICE_IP": "device_ip",
        "DEVICE_PORT": ("device_port", int),
        "CC_TMUX_SESSION": "tmux_session",
        "HOOK_HOST": "hook_host",
        "HOOK_PORT": ("hook_port", int),
        "PERMISSION_TIMEOUT": ("permission_timeout", float),
    }
    for env_key, target in env_map.items():
        raw = os.environ.get(env_key)
        if raw is None:
            continue
        if isinstance(target, tuple):
            name, caster = target
            setattr(cfg, name, caster(raw))
        else:
            setattr(cfg, target, raw)

    return cfg
