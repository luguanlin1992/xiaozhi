"""向固件 CcNotifyServer 推送本地提示（铃声 + 屏幕）。失败仅记日志，不阻断主流程。"""
from __future__ import annotations

import asyncio
import logging

import aiohttp

from .config import Config

log = logging.getLogger("cc-bridge.device")


class DeviceNotifier:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg

    async def notify(self, level: str, title: str, text: str) -> None:
        if not self.cfg.device_ip:
            log.debug("device_ip 未配置，跳过设备提示")
            return
        url = f"http://{self.cfg.device_ip}:{self.cfg.device_port}/notify"
        payload = {"level": level, "title": title, "text": text[:200]}
        # 设备开了 WiFi modem-sleep，第一个连接常被丢弃；重试几次以唤醒它。
        timeout = aiohttp.ClientTimeout(total=6)
        last_exc: Exception | None = None
        for attempt in range(3):
            try:
                async with aiohttp.ClientSession(timeout=timeout) as session:
                    async with session.post(url, json=payload) as resp:
                        await resp.read()
                        if resp.status != 200:
                            log.warning("设备返回 %s", resp.status)
                        return
            except Exception as exc:  # noqa: BLE001 - 推送失败不应影响主流程
                last_exc = exc
                await asyncio.sleep(0.5 * (attempt + 1))
        log.warning("推送设备提示失败(已重试): %s", last_exc)
