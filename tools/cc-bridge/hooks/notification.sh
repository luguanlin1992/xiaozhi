#!/usr/bin/env bash
# Claude Code Notification hook：转发给 cc-bridge（非阻塞，仅触发设备提示）。
URL="${CC_BRIDGE_URL:-http://127.0.0.1:8788}"
exec curl -sS -m 10 -X POST "$URL/notification" \
  -H 'content-type: application/json' --data-binary @-
