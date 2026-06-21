"""Bridge：连接 hook 事件、设备提示、MCP 工具与 tmux 注入的中枢。"""
from __future__ import annotations

import asyncio
import logging
import uuid

from .config import Config
from .device import DeviceNotifier
from .state import PendingPermission, State
from .tmux import TmuxInjector

log = logging.getLogger("cc-bridge")


class Bridge:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg
        self.state = State()
        self.device = DeviceNotifier(cfg)
        self.tmux = TmuxInjector(cfg.tmux_session)

    # ── 来自 Claude Code hook 的事件 ────────────────────────────────

    async def on_pretooluse(self, payload: dict) -> dict:
        """阻塞，直到语音批准/拒绝或超时。返回 PreToolUse hook 的 JSON 输出。"""
        tool_name = payload.get("tool_name", "?")
        tool_input = payload.get("tool_input", {}) or {}

        # 若已有挂起请求，先放行旧的为 ask（让其回退到终端手动）
        if self.state.current_permission is not None and not self.state.current_permission.future.done():
            self.state.current_permission.future.set_result(("ask", "被新的权限请求取代"))

        loop = asyncio.get_running_loop()
        pending = PendingPermission(
            id=uuid.uuid4().hex[:8],
            tool_name=tool_name,
            tool_input=tool_input,
            future=loop.create_future(),
        )
        self.state.current_permission = pending
        self.state.running = True
        log.info("PreToolUse 等待裁决: %s", pending.summary())

        await self.device.notify("permission", "需要批准", pending.summary())

        try:
            decision, reason = await asyncio.wait_for(pending.future, timeout=self.cfg.permission_timeout)
        except asyncio.TimeoutError:
            decision, reason = "ask", "语音裁决超时，回退到终端"
        finally:
            if self.state.current_permission is pending:
                self.state.current_permission = None

        return {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": decision,
                "permissionDecisionReason": reason,
            }
        }

    async def on_notification(self, payload: dict) -> None:
        message = payload.get("message", "") or "Claude 需要你的关注"
        self.state.add_event("question", message)
        await self.device.notify("question", "Claude 提问", message)

    async def on_stop(self, payload: dict) -> None:
        self.state.running = False
        result = _last_assistant_text(payload.get("transcript_path"))
        if result:
            self.state.last_result = result
        self.state.add_event("done", result or "本轮已结束")
        await self.device.notify("done", "已完成", result or "Claude 本轮已结束")

    # ── 来自 MCP 工具（语音）的指令 ────────────────────────────────

    def tool_status(self) -> str:
        return self.state.pending_summary()

    def tool_approve(self) -> str:
        pending = self.state.current_permission
        if pending is None or pending.future.done():
            return "当前没有待批准的权限请求。"
        pending.future.set_result(("allow", "用户语音批准"))
        return f"已批准：{pending.summary()}"

    def tool_deny(self, reason: str = "") -> str:
        pending = self.state.current_permission
        if pending is None or pending.future.done():
            return "当前没有待批准的权限请求。"
        pending.future.set_result(("deny", reason or "用户语音拒绝"))
        return f"已拒绝：{pending.summary()}"

    async def tool_answer(self, text: str) -> str:
        if not text:
            return "回答内容为空。"
        ok = await self.tmux.send_text(text, submit=True)
        return "已把回答发给 Claude。" if ok else "注入失败：检查 tmux 会话是否存在。"

    async def tool_send(self, text: str) -> str:
        if not text:
            return "指令内容为空。"
        ok = await self.tmux.send_text(text, submit=True)
        if ok:
            self.state.running = True
        return "已把指令发给 Claude。" if ok else "注入失败：检查 tmux 会话是否存在。"

    async def tool_interrupt(self) -> str:
        ok = await self.tmux.interrupt()
        return "已发送中断。" if ok else "中断失败：检查 tmux 会话是否存在。"


def _last_assistant_text(transcript_path: str | None) -> str:
    """尽力从 transcript（jsonl）尾部取最后一条 assistant 文本，用于播报结果摘要。"""
    if not transcript_path:
        return ""
    try:
        import json
        from pathlib import Path

        lines = Path(transcript_path).read_text(encoding="utf-8").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            if obj.get("type") != "assistant":
                continue
            content = obj.get("message", {}).get("content", [])
            texts = [c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text"]
            joined = " ".join(t for t in texts if t).strip()
            if joined:
                return joined[:200]
    except Exception:  # noqa: BLE001 - 摘要是锦上添花，失败就算了
        return ""
    return ""
