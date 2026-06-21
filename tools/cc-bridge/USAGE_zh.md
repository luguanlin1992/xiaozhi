# 小智语音控制 Claude Code · 使用说明

用小智(esp32 设备)语音指挥跑在你电脑上的交互式 Claude Code：
需权限/提问/结束时设备**响铃 + 屏幕提示**，你**说话**就能批准权限、回答问题、下达指令。

```
你说话 → 小智云(ASR/LLM) → 调 claude_code_* 工具 → cc-bridge → tmux 注入 → Claude Code
Claude Code → hooks → cc-bridge → HTTP → 小智设备(响铃/屏幕)
```

---

## 一、组成

| 部分 | 位置 | 作用 |
| --- | --- | --- |
| 固件 `CcNotifyServer` | esp32 设备(已刷 `claude-code-voice` 分支) | 收 `POST /notify` → 本地响铃+屏幕 |
| cc-bridge | `tools/cc-bridge`(本机 Python) | 连小智云 MCP 接入点 + 收 hooks + 注入 tmux + 推送设备 |
| 受控会话 | tmux 会话 `cc` 里的 `claude` | 真正干活的 Claude Code |

---

## 二、一次性准备

1. **固件**：设备已刷好并联网（启动日志有 `Notify server started on port 8930`，记下设备 IP）。
2. **依赖**：
   ```bash
   cd tools/cc-bridge
   python3 -m venv .venv && ./.venv/bin/python -m pip install -r requirements.txt
   ```
3. **配置** `tools/cc-bridge/config.toml`（从 `config.example.toml` 复制）：
   - `xiaozhi_mcp_endpoint`：xiaozhi.me 后台 → MCP 接入点 的 `wss://...?token=...`
   - `device_ip`：设备局域网 IP
   - `tmux_session`：`cc`（与下面脚本一致）

> ⚠ `config.toml` 含 token，已被 `.gitignore` 忽略；token 泄露能控制你的小智 agent，必要时到后台重置。

---

## 三、启动（每次使用）

**① 起 bridge**（一个常驻终端）：
```bash
cd tools/cc-bridge
PYTHONPATH=. ./.venv/bin/python -m cc_bridge
```
看到 `已连接小智 MCP 接入点` + `MCP 握手: tools/list` 即为就绪。

**② 在想要的工作目录起受控会话**：
```bash
cd tools/cc-bridge
./start-cc.sh ~/cc-voice-demo      # 或换成任意目录
```
脚本会：在该目录写好 hooks 配置 → 杀掉旧 `cc` 会话 → 在该目录里启动 `claude`（detached）。
首次进新目录会问“是否信任”，按提示执行 `tmux send-keys -t cc Enter` 确认。

---

## 四、日常使用（对小智说话）

| 你说 | 触发工具 | 效果 |
| --- | --- | --- |
| “帮我创建一个网页页面” | `claude_code_send` | 把这句话作为 prompt 注入会话，claude 开始干 |
| “告诉 Claude，把 README 翻成英文” | `claude_code_send` | 同上 |
| （claude 在等你回答时口述答案） | `claude_code_answer` | 把回答注入会话 |
| “批准 / 同意 / 可以” | `claude_code_approve` | 放行当前权限请求 |
| “拒绝 / 不行” | `claude_code_deny` | 拒绝当前权限请求 |
| “Claude 那边怎么样了” | `claude_code_status` | 播报待办/状态/最近结果 |
| “停下 / 打断” | `claude_code_interrupt` | 给会话发中断 |

**典型权限闭环**：说“创建网页” → claude 要写文件 → **设备响铃**(PreToolUse 阻塞) → 你说“批准” → claude 写文件 → 结束 → 设备播完成提示。

---

## 五、控制 / 切换工作目录

- **工作目录 = 启动 `cc` 会话时所在的目录**，claude 启动后无法中途切换。
- 换目录：重跑 `./start-cc.sh <新目录>`（会重建会话）。
- 想在你**已有的某个项目**里语音干活：`./start-cc.sh ~/path/to/项目`。该目录若已有 `.claude/settings.json`，脚本不会覆盖，会提示你把 `settings.example.json` 的 hooks 段手动合并进去。
- **文件“跑到别的地方”（如桌面）**：那是 claude 自己选了绝对路径，不是工作目录错。让它放当前目录就明确说“**在当前项目目录里创建**”，或先 `claude_code_status` 看它做了什么。

---

## 六、查看会话在做什么

- 真实终端里：`tmux attach -t cc`（看完按 `Ctrl-b` 再按 `d` 脱离，**别 `exit`**）。
- 不 attach 也照常工作（bridge 直接注入，跟你看不看无关）。

---

## 七、常见问题

| 现象 | 原因 / 解决 |
| --- | --- |
| 小智说“Claude Code 用不了 / 注入失败” | 没有 `cc` 会话。先 `./start-cc.sh`。 |
| `tmux new/attach` 报 `not a terminal` | 当前 shell 没有 TTY。用 `./start-cc.sh`（detached 启动），或换真正的 Terminal/iTerm 再 attach。 |
| `duplicate session: cc` | 会话已存在，用 `tmux attach -t cc`，不要再 `new`。 |
| 说了指令但设备不响 | bridge 没起 / `device_ip` 不对；设备 WiFi 休眠会丢首个连接，bridge 已自动重试。 |
| 权限说了“批准”没反应 | 确认 bridge 在跑、`cc` 会话里 claude 确实弹出了权限（PreToolUse 仅拦 `Bash/Edit/Write/NotebookEdit/mcp__*`）。 |
| 设备重启崩溃 | 已修复（通知服务在网络栈初始化后才启动）。 |

---

## 八、关掉

```bash
tmux kill-session -t cc      # 关受控会话
# bridge 那个终端 Ctrl-C
```
