# 用小智语音控制 Claude Code

> 本功能位于分支 `claude-code-voice`。在上游小智固件之上，新增「让运行在终端里的交互式
> Claude Code 会话与小智设备双向联动」的能力：Claude Code 需要权限 / 提问 / 执行结束时，
> 小智设备**本地响铃 + 屏幕提示**；你**对小智说话**即可批准权限、回答问题、下达新指令。

- 面向使用者的完整操作手册：[`tools/cc-bridge/USAGE_zh.md`](../tools/cc-bridge/USAGE_zh.md)
- 桥接架构与开发说明：[`tools/cc-bridge/README.md`](../tools/cc-bridge/README.md)
- 一键启动受控会话脚本：[`tools/cc-bridge/start-cc.sh`](../tools/cc-bridge/start-cc.sh)

---

## 一、它解决什么问题

平时用 Claude Code，你得盯着终端：它要权限时你按键批准，它提问时你打字回答。
本功能把这套「看屏幕 + 打字」换成「**听响铃 + 说话**」——

```
你说话 → 小智云(ASR/LLM) → 调 claude_code_* 工具 → cc-bridge → tmux 注入 → Claude Code
Claude Code → hooks → cc-bridge → HTTP /notify → 小智设备(本地响铃 / 屏幕提示)
```

适合：边做别的事边让 Claude Code 跑长任务，靠设备响铃把你拉回来用一句话决策。

---

## 二、三个组成部分

| 部分 | 位置 | 作用 |
| --- | --- | --- |
| 固件 `CcNotifyServer` | esp32 设备（本分支固件） | 监听 `POST /notify` → 调 `Application::Alert()` 本地响铃 + 屏幕显示 |
| cc-bridge | `tools/cc-bridge/`（本机 Python） | 连小智云 MCP 接入点 + 收 Claude Code hooks + tmux 注入 + 推送设备 |
| 受控会话 | tmux 会话 `cc` 里的 `claude` | 真正干活的交互式 Claude Code |

### 三条数据流

1. **主动提示**：Claude Code 触发 `Notification` / `Stop` hook → bridge `POST /notify` →
   设备用现成的 `Application::Alert()` 播本地铃声 + 屏幕显示「需要批准 / 已完成」。
2. **批准权限（最稳，不依赖键盘注入）**：`PreToolUse` hook **同步阻塞**（最长 600s）→
   bridge 挂起请求并推送设备提示 → 你语音「批准/拒绝」→ 云端 LLM 调
   `claude_code_approve` / `claude_code_deny` → bridge 把 `permissionDecision` 返回给
   Claude Code → 它继续或被拒。超时回退为 `ask`（终端里手动决定）。
3. **回答 / 下指令**：你语音 → 云端 LLM 调 `claude_code_answer` / `claude_code_send` →
   bridge 经 `tmux send-keys` 注入到交互会话。

---

## 三、固件侧改动（相对上游）

- 新增 [`main/cc_notify_server.{h,cc}`](../main/cc_notify_server.cc)：基于 `esp_http_server` 的
  轻量单向通知端点，`POST /notify` body `{"level":"permission|question|done","title":"..","text":".."}`，
  按 `level` 映射表情（permission/question→`thinking`，done→`happy`），提示音统一用自定义的
  `Lang::Sounds::OGG_CC_PERMISSION`（见下「自定义提示音」）。
- [`main/boards/bread-compact-wifi-lcd/config.h`](../main/boards/bread-compact-wifi-lcd/config.h)：
  新增音频块 + `CC_NOTIFY_ENABLED` / `CC_NOTIFY_PORT`（默认 8930）开关。
- [`compact_wifi_board_lcd.cc`](../main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc)：
  在 `StartNetwork()` 覆写里、**网络栈初始化之后**才启动通知服务
  （在构造函数里启动会触发 `Invalid mbox` 启动崩溃）。
- [`main/CMakeLists.txt`](../main/CMakeLists.txt)：`SOURCES` 追加 `cc_notify_server.cc`，
  `PRIV_REQUIRES` 追加 `esp_http_server`。

> 构建环境：ESP-IDF ≥ 5.5.2，目标 `esp32s3`，板型 `bread-compact-wifi-lcd`。

### 自定义提示音

提示音是嵌进固件的 `.ogg`（放在 `main/assets/common/`）。构建时 `scripts/gen_lang.py`
会自动扫描该目录，给每个文件生成一个 `OGG_<文件名大写>` 常量并由 CMake `EMBED_FILES`
打进固件——**新增一个 ogg 文件就多一个可用常量，无需手改头文件**。

固件播放器只认特定格式（Opus 编码 / 单声道 / 16kHz），所以任何素材都要先转一遍：

```bash
# 从 in.mp3 的第 85 秒起截 6 秒，转成固件要求的格式
ffmpeg -y -ss 85 -to 91 -i in.mp3 \
  -c:a libopus -b:a 16k -ac 1 -ar 16000 -frame_duration 60 \
  main/assets/common/cc_permission.ogg
```

当前 permission / question / done 三种通知都用 `main/assets/common/cc_permission.ogg`
（对应常量 `OGG_CC_PERMISSION`，在 [`cc_notify_server.cc`](../main/cc_notify_server.cc)
的 `ShowNotification` 里设定）。要换音：替换该 `.ogg` 文件后 `idf.py build flash` 即可；
想给不同 level 用不同音，把多个 ogg 放进 `common/`，在 `ShowNotification` 里分别引用对应
的 `OGG_<NAME>` 常量。

---

## 四、快速开始

详见 [`tools/cc-bridge/USAGE_zh.md`](../tools/cc-bridge/USAGE_zh.md)，最小步骤：

```bash
# 0) 设备已刷本分支固件并联网（启动日志有 "Notify server started on port 8930"）

# 1) 装桥接依赖、填配置
cd tools/cc-bridge
python3 -m venv .venv && ./.venv/bin/python -m pip install -r requirements.txt
cp config.example.toml config.toml   # 填 xiaozhi_mcp_endpoint / device_ip

# 2) 起 bridge（常驻终端）
PYTHONPATH=. ./.venv/bin/python -m cc_bridge

# 3) 在想要的工作目录起受控会话
./start-cc.sh ~/cc-voice-demo
```

随后对小智说话即可：「帮我创建一个网页」「批准」「拒绝」「Claude 那边怎么样了」「停下」。

---

## 五、已知限制

- **主动播报靠设备本地铃声 + 屏幕文字**，不是云端语音念整句——官方云没有面向终端用户的
  「向指定设备主动下发 TTS」API。被提示后你开口走云端对话。
- **tmux 注入**依赖会话名与前台输入框状态；权限路径已绕开注入（走阻塞 hook），
  脆弱面仅限自由文本的回答 / 下指令。
- 设备 IP / 端口为手工配置；鉴权、mDNS 自动发现留待后续。
- `config.toml` 含 MCP 接入点 token，已被 `.gitignore` 忽略；token 泄露可控制你的小智 agent，
  必要时到 xiaozhi.me 后台重置。
