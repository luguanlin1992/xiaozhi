"""作为 MCP server 连接小智官方云的 MCP 接入点（WebSocket + JSON-RPC 2.0）。

云端 LLM 作为 MCP client：发 initialize / tools/list / tools/call，
我们在这里响应并把 tools/call 路由到 Bridge。
"""
from __future__ import annotations

import asyncio
import json
import logging

import websockets

from .bridge import Bridge

log = logging.getLogger("cc-bridge.mcp")

PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "cc-bridge", "version": "0.1.0"}

TOOLS = [
    {
        "name": "claude_code_status",
        "description": "查询本机 Claude Code 的当前状态与待办：是否有待批准的权限、Claude 提出的问题、最近的执行结果。当用户问“Claude 那边怎么样了/有什么消息吗/进展如何”时调用。",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "claude_code_approve",
        "description": "批准 Claude Code 当前挂起的权限请求（让它执行那条命令/编辑）。当用户说“批准/同意/可以/允许/让它做”时调用。",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "claude_code_deny",
        "description": "拒绝 Claude Code 当前挂起的权限请求。当用户说“拒绝/不行/不要/别做”时调用。",
        "inputSchema": {
            "type": "object",
            "properties": {"reason": {"type": "string", "description": "拒绝原因（可选）"}},
        },
    },
    {
        "name": "claude_code_answer",
        "description": "回答 Claude Code 向用户提出的问题，把用户的回答原样发给它。当 Claude 在等待用户回答、用户口述了答案时调用。",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string", "description": "要发给 Claude 的回答文本"}},
            "required": ["text"],
        },
    },
    {
        "name": "claude_code_send",
        "description": "给 Claude Code 下达一条新的指令/任务（相当于在终端里输入一段 prompt 并回车）。当用户说“告诉 Claude……/让 Claude……/帮我让它做……”时调用。",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string", "description": "要发给 Claude 的指令文本"}},
            "required": ["text"],
        },
    },
    {
        "name": "claude_code_interrupt",
        "description": "中断 Claude Code 当前正在进行的工作。当用户说“停下/打断/取消”时调用。",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


class McpEndpoint:
    def __init__(self, bridge: Bridge) -> None:
        self.bridge = bridge
        self.endpoint = bridge.cfg.xiaozhi_mcp_endpoint

    async def run_forever(self) -> None:
        if not self.endpoint:
            log.warning("未配置 XIAOZHI_MCP_ENDPOINT，MCP 接入点未启动（仅 hook/设备提示可用）")
            await asyncio.Event().wait()  # 保持进程存活，继续提供 hook/设备提示
            return
        backoff = 1.0
        while True:
            try:
                async with websockets.connect(self.endpoint, max_size=2**20) as ws:
                    log.info("已连接小智 MCP 接入点")
                    backoff = 1.0
                    await self._serve(ws)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                log.warning("MCP 接入点断开: %s；%.0fs 后重连", exc, backoff)
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 30.0)

    async def _serve(self, ws) -> None:
        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue
            response = await self._handle(msg)
            if response is not None:
                await ws.send(json.dumps(response, ensure_ascii=False))

    async def _handle(self, msg: dict) -> dict | None:
        method = msg.get("method")
        msg_id = msg.get("id")

        # 通知（无 id）不需要响应
        if method == "notifications/initialized" or (method and method.startswith("notifications/")):
            return None

        if method in ("initialize", "tools/list"):
            log.info("MCP 握手: %s", method)
        if method == "initialize":
            return _ok(msg_id, {
                "protocolVersion": msg.get("params", {}).get("protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": SERVER_INFO,
            })

        if method == "ping":
            return _ok(msg_id, {})

        if method == "tools/list":
            return _ok(msg_id, {"tools": TOOLS})

        if method == "tools/call":
            params = msg.get("params", {}) or {}
            name = params.get("name", "")
            args = params.get("arguments", {}) or {}
            log.info("语音调用工具: %s %s", name, args)
            try:
                text = await self._dispatch(name, args)
                return _ok(msg_id, {"content": [{"type": "text", "text": text}]})
            except Exception as exc:  # noqa: BLE001
                log.exception("工具 %s 执行失败", name)
                return _ok(msg_id, {"content": [{"type": "text", "text": f"执行失败：{exc}"}], "isError": True})

        if msg_id is not None:
            return _err(msg_id, -32601, f"method not found: {method}")
        return None

    async def _dispatch(self, name: str, args: dict) -> str:
        b = self.bridge
        if name == "claude_code_status":
            return b.tool_status()
        if name == "claude_code_approve":
            return b.tool_approve()
        if name == "claude_code_deny":
            return b.tool_deny(args.get("reason", ""))
        if name == "claude_code_answer":
            return await b.tool_answer(args.get("text", ""))
        if name == "claude_code_send":
            return await b.tool_send(args.get("text", ""))
        if name == "claude_code_interrupt":
            return await b.tool_interrupt()
        return f"未知工具：{name}"


def _ok(msg_id, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def _err(msg_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}
