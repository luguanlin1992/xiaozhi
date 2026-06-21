# cc-bridge

把**运行在终端里的交互式 Claude Code 会话**桥接到小智设备：

- Claude Code 需要权限 / 提问 / 执行结束 → 小智设备本地响铃 + 屏幕提示。
- 你对小智说话 → 云端 LLM 调用本桥接暴露的 MCP 工具 → 批准权限 / 回答问题 / 下达新指令 / 中断。

## 架构

```
交互式 Claude Code (tmux 会话 cc)
  │ hooks(PreToolUse 阻塞 / Notification / Stop)        ▲ tmux send-keys 注入
  ▼                                                     │
cc-bridge (本机 Python)  ──HTTP /notify──▶ 小智设备(固件 CcNotifyServer) 本地响铃+屏幕
  │ 作为 MCP server 连接小智云 MCP 接入点(WebSocket)
  ▼
小智官方云 (ASR/LLM/TTS) ◀──语音对话──▶ 小智设备
```

- **权限**：`PreToolUse` hook 同步阻塞（最长 600s），bridge 挂起请求并等你语音「批准/拒绝」，再把 `permissionDecision` 返回给 Claude Code。**不依赖键盘注入**。超时则回退为 `ask`（终端里手动决定）。
- **回答/下指令**：经 `tmux send-keys` 注入到交互式会话（依赖 tmux）。

## 前置条件

- Python ≥ 3.11（用到 `tomllib`）。
- `tmux`。
- 一台已联网、刷了本仓库 `claude-code-voice` 分支固件（含 `CcNotifyServer`）的小智设备，且与本机同局域网。
- 小智官方云账号，已在后台创建 **MCP 接入点** 拿到 `wss://api.xiaozhi.me/mcp/?token=...`。

## 安装

```bash
cd tools/cc-bridge
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp config.example.toml config.toml   # 填写 endpoint / device_ip
chmod +x hooks/*.sh
```

## 配置 Claude Code hooks

把 `settings.example.json` 里的 `hooks` 合并进你**项目**的 `.claude/settings.json`，把 `/ABS/PATH`
换成 `tools/cc-bridge` 的绝对路径。`PreToolUse` 的 `matcher` 决定哪些工具走语音批准
（默认 `Bash|Edit|Write|NotebookEdit|mcp__.*`，只读工具照常放行；想更省心可只留 `Bash`）。

> 若你的 Claude Code 版本支持 `type:"http"` hook，也可直接把 command 换成
> `{ "type": "http", "url": "http://127.0.0.1:8788/pretooluse", "timeout": 600 }`，省去脚本。

## 运行

```bash
# 1) 起桥接
cd tools/cc-bridge && source .venv/bin/activate
python -m cc_bridge

# 2) 在 tmux 里跑 Claude Code（会话名要与 config 的 tmux_session 一致）
tmux new -s cc claude
```

## 端到端验证

1. **设备提示**：`curl -XPOST http://<device_ip>:8930/notify -H 'content-type: application/json' \
   -d '{"level":"permission","title":"Claude","text":"需要运行 npm install"}'` → 设备响铃 + 屏幕显示。
2. **桥接健康**：`curl http://127.0.0.1:8788/health`。
3. **权限闭环**：在 tmux 的 claude 里让它跑一条会触发权限的命令 → 设备响铃 → 对小智说「批准」→ 命令执行。说「拒绝」→ 被拒。
4. **指令闭环**：对小智说「告诉 Claude，把 README 翻译成英文」→ 注入到会话 → Claude 开始执行；结束后设备播完成提示。
5. **状态查询**：对小智说「Claude 那边怎么样了」→ 云端调 `claude_code_status` 播报。

## MCP 工具

| 工具 | 作用 | 触发语 |
| --- | --- | --- |
| `claude_code_status` | 查询待办/状态/最近结果 | “怎么样了/有消息吗” |
| `claude_code_approve` | 批准当前权限请求 | “批准/同意/可以” |
| `claude_code_deny` | 拒绝当前权限请求 | “拒绝/不行” |
| `claude_code_answer` | 回答 Claude 的提问 | （口述答案） |
| `claude_code_send` | 下达新指令 | “告诉 Claude……” |
| `claude_code_interrupt` | 中断当前工作 | “停下/打断” |

## 已知限制

- **主动播报靠设备本地铃声+屏幕文字**，不是云端语音念整句（官方云无面向用户的主动 TTS 下发）。被提示后你开口走云端对话。
- **tmux 注入**依赖会话名与前台输入框状态；权限路径已绕开注入（走阻塞 hook），脆弱面仅限自由文本回答/下指令。
- 端口/IP 为手工配置；鉴权、mDNS 自动发现留待后续。
