#!/usr/bin/env bash
# 在指定工作目录启动「可被小智语音控制」的交互式 Claude Code 会话。
#
# 用法:
#   ./start-cc.sh                # 默认工作目录 ~/cc-voice-demo
#   ./start-cc.sh ~/projects/foo # 在 ~/projects/foo 里启动
#
# 工作目录 = claude 的主工作目录。claude 启动后无法中途切换，换目录就重跑本脚本。
set -euo pipefail

BRIDGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # tools/cc-bridge
HOOKS_DIR="$BRIDGE_DIR/hooks"
SESSION="${CC_TMUX_SESSION:-cc}"

WORKDIR="${1:-$HOME/cc-voice-demo}"
mkdir -p "$WORKDIR"
WORKDIR="$(cd "$WORKDIR" && pwd)"

# 1) 确保目标目录带上指向 cc-bridge 的 hooks（PreToolUse/Notification/Stop）
mkdir -p "$WORKDIR/.claude"
SETTINGS="$WORKDIR/.claude/settings.json"
if [ ! -f "$SETTINGS" ]; then
  cat > "$SETTINGS" <<JSON
{
  "hooks": {
    "PreToolUse": [
      { "matcher": "Bash|Edit|Write|NotebookEdit|mcp__.*",
        "hooks": [ { "type": "command", "command": "$HOOKS_DIR/pretooluse.sh", "timeout": 600 } ] }
    ],
    "Notification": [
      { "hooks": [ { "type": "command", "command": "$HOOKS_DIR/notification.sh", "timeout": 10 } ] }
    ],
    "Stop": [
      { "hooks": [ { "type": "command", "command": "$HOOKS_DIR/stop.sh", "timeout": 10 } ] }
    ]
  }
}
JSON
  echo "✓ 已生成 hooks 配置: $SETTINGS"
elif ! grep -q "cc-bridge/hooks" "$SETTINGS"; then
  echo "⚠ $SETTINGS 已存在但没有 cc-bridge hooks。"
  echo "  请把 $BRIDGE_DIR/settings.example.json 里的 hooks 段手动合并进去（避免覆盖你已有配置）。"
fi

# 2) 重建 tmux 会话（在目标工作目录里启动 claude）
tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" -x 220 -y 50 -c "$WORKDIR" claude
echo "✓ claude 已在 $WORKDIR 启动 (tmux 会话: $SESSION)"
echo
echo "下一步:"
echo "  · 首次进新目录会问是否信任，按一下确认:  tmux send-keys -t $SESSION Enter"
echo "  · 想亲眼看会话(需真实终端):              tmux attach -t $SESSION  (脱离: Ctrl-b 再按 d)"
echo "  · 之后直接对小智说话即可指挥这个会话。"
