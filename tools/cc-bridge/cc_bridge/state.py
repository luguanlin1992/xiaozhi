"""桥接的运行时状态：待办事件 + 当前挂起的权限请求。"""
from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class PendingPermission:
    """一个被 PreToolUse hook 阻塞、等待语音裁决的权限请求。"""
    id: str
    tool_name: str
    tool_input: dict
    future: "asyncio.Future[tuple[str, str]]"  # (decision, reason) decision in {allow, deny}
    created_at: float = field(default_factory=time.time)

    def summary(self) -> str:
        detail = ""
        if self.tool_name == "Bash":
            detail = self.tool_input.get("command", "")
        elif self.tool_name in ("Edit", "Write"):
            detail = self.tool_input.get("file_path", "")
        detail = detail[:120]
        return f"{self.tool_name}: {detail}".strip().rstrip(":")


@dataclass
class Event:
    kind: str       # permission | question | done | info
    text: str
    at: float = field(default_factory=time.time)


class State:
    def __init__(self) -> None:
        self.current_permission: Optional[PendingPermission] = None
        self.events: list[Event] = []
        self.last_result: str = ""
        self.running: bool = False

    def add_event(self, kind: str, text: str) -> None:
        self.events.append(Event(kind, text))
        # 只保留最近 20 条
        self.events = self.events[-20:]

    def pending_summary(self) -> str:
        parts: list[str] = []
        if self.current_permission is not None:
            parts.append(f"待批准权限：{self.current_permission.summary()}")
        recent = [e for e in self.events if e.kind in ("question", "done")][-3:]
        for e in recent:
            label = {"question": "Claude 提问", "done": "已完成"}.get(e.kind, e.kind)
            parts.append(f"{label}：{e.text}")
        if self.last_result:
            parts.append(f"最近结果：{self.last_result}")
        if not parts:
            return "Claude Code 暂无待办事项。" + ("正在运行。" if self.running else "")
        return "；".join(parts)
