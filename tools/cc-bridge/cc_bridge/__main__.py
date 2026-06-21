"""入口：同时跑 hook 接收服务与小智 MCP 接入点客户端。"""
from __future__ import annotations

import asyncio
import logging

from . import config, hook_server
from .bridge import Bridge
from .mcp_endpoint import McpEndpoint


async def amain() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )
    cfg = config.load()
    bridge = Bridge(cfg)

    runner = await hook_server.start(bridge)
    mcp = McpEndpoint(bridge)
    try:
        await mcp.run_forever()
    finally:
        await runner.cleanup()


def main() -> None:
    try:
        asyncio.run(amain())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
