"""把文本/控制键注入到运行交互式 claude 的 tmux 会话。

依赖：用户用 `tmux new -s <session> claude` 启动会话。
"""
from __future__ import annotations

import asyncio
import logging

log = logging.getLogger("cc-bridge.tmux")


class TmuxInjector:
    def __init__(self, session: str) -> None:
        self.session = session

    async def _run(self, *args: str) -> bool:
        proc = await asyncio.create_subprocess_exec(
            "tmux", *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        _, stderr = await proc.communicate()
        if proc.returncode != 0:
            log.warning("tmux %s 失败: %s", " ".join(args), stderr.decode(errors="replace").strip())
            return False
        return True

    async def session_exists(self) -> bool:
        return await self._run("has-session", "-t", self.session)

    async def send_text(self, text: str, submit: bool = True) -> bool:
        """注入一段文本，submit=True 时随后回车提交。"""
        # -l 字面量发送，避免 tmux 把内容解释成键名
        ok = await self._run("send-keys", "-t", self.session, "-l", text)
        if ok and submit:
            # 与字面文本分开发送回车，确保被解释为 Enter
            ok = await self._run("send-keys", "-t", self.session, "Enter")
        return ok

    async def send_keys(self, *keys: str) -> bool:
        """发送命名按键，如 Escape、C-c。"""
        return await self._run("send-keys", "-t", self.session, *keys)

    async def interrupt(self) -> bool:
        # 先 Escape（取消当前菜单/输入），再 Escape 兜底
        return await self.send_keys("Escape")
