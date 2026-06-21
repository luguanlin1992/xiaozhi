#!/usr/bin/env bash
# Claude Code Stop hook：转发给 cc-bridge（标记完成并触发设备提示）。
URL="${CC_BRIDGE_URL:-http://127.0.0.1:8788}"
exec curl -sS -m 10 -X POST "$URL/stop" \
  -H 'content-type: application/json' --data-binary @-
