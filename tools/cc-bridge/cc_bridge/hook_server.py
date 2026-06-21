"""接收 Claude Code 的 hook（type:"http"）：PreToolUse / Notification / Stop。"""
from __future__ import annotations

import logging

from aiohttp import web

from .bridge import Bridge

log = logging.getLogger("cc-bridge.hook")


def build_app(bridge: Bridge) -> web.Application:
    app = web.Application()

    async def pretooluse(request: web.Request) -> web.Response:
        payload = await _json(request)
        result = await bridge.on_pretooluse(payload)
        return web.json_response(result)

    async def notification(request: web.Request) -> web.Response:
        payload = await _json(request)
        await bridge.on_notification(payload)
        return web.json_response({"ok": True})

    async def stop(request: web.Request) -> web.Response:
        payload = await _json(request)
        await bridge.on_stop(payload)
        return web.json_response({"ok": True})

    async def health(_: web.Request) -> web.Response:
        return web.json_response({"ok": True, "status": bridge.tool_status()})

    app.add_routes([
        web.post("/pretooluse", pretooluse),
        web.post("/notification", notification),
        web.post("/stop", stop),
        web.get("/health", health),
    ])
    return app


async def _json(request: web.Request) -> dict:
    try:
        data = await request.json()
        return data if isinstance(data, dict) else {}
    except Exception:  # noqa: BLE001
        return {}


async def start(bridge: Bridge) -> web.AppRunner:
    app = build_app(bridge)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, bridge.cfg.hook_host, bridge.cfg.hook_port)
    await site.start()
    log.info("hook 服务已启动 http://%s:%d", bridge.cfg.hook_host, bridge.cfg.hook_port)
    return runner
