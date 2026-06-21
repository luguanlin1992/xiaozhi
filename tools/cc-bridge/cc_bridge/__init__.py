"""cc-bridge: 把交互式 Claude Code 会话桥接到小智设备。

- hook_server: 接收 Claude Code 的 PreToolUse(阻塞)/Notification/Stop hook。
- mcp_endpoint: 作为 MCP server 连接小智官方云 MCP 接入点，暴露 claude_code_* 工具。
- device: 向固件 CcNotifyServer 推送本地提示。
- tmux: 把语音转来的回答/指令注入交互式会话。
"""

__all__ = ["config", "state", "device", "tmux", "hook_server", "mcp_endpoint", "bridge"]
