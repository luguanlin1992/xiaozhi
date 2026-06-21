#!/usr/bin/env bash
# Claude Code PreToolUse hook：把事件转发给 cc-bridge，阻塞等待语音裁决，
# 把 bridge 返回的 JSON（permissionDecision）原样输出给 Claude Code。
URL="${CC_BRIDGE_URL:-http://127.0.0.1:8788}"
exec curl -sS -m 600 -X POST "$URL/pretooluse" \
  -H 'content-type: application/json' --data-binary @-
