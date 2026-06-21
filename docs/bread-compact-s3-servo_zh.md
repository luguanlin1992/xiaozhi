# ESP32-S3 面包板 + LCD + PCA9685 舵机 — 接线与烧录指南

本文档面向以下硬件组合，在小智 AI（`bread-compact-wifi-lcd` 板型）上实现语音控制四轮舵机小车：

| 器件 | 说明 |
|------|------|
| 主控 | ESP32-S3-N16R8 开发板 |
| 音频 | LMD2718 + NS4168 音频模块（I2S 麦克风 + I2S 功放） |
| 屏幕 | 240×240 ST7789 SPI LCD |
| 舵机驱动 | PCA9685 16 路 PWM 模块 |
| 执行器 | 4× MG90S **360° 连续旋转**舵机 |

固件已在工程中集成 PCA9685 驱动与 MCP 舵机工具，配置文件见  
`main/boards/bread-compact-wifi-lcd/config.h`。

---

## 1. 总体接线示意

```
                    ┌─────────────────────┐
                    │   ESP32-S3-N16R8    │
                    │                     │
    ST7789 LCD ────►│ SPI + 背光          │
    音频       ────►│ GPIO 4~7, 15（PDM麦+I2S喇叭）│
    PCA9685    ────►│ I2C  SDA=10 SCL=9   │
    PCA9685 OE ────►│ GPIO 8（关断 PWM）   │
                    └──────────┬──────────┘
                               │ I2C
                    ┌──────────▼──────────┐
                    │      PCA9685        │
                    │  CH0~3 ──► 4舵机    │
                    └──────────┬──────────┘
                               │ 信号线
              ┌────────────────┼────────────────┐
              │    FL(0)   FR(1)   RL(2)   RR(3) │
              └────────────────────────────────┘
                               │
                    ┌──────────▼──────────┐
                    │  5V 舵机专用电源     │  （勿用 USB 单独带 4 个舵机）
                    └─────────────────────┘
```

**电源原则**

- ESP32、屏幕逻辑、PCA9685 的 **VCC（逻辑）** 可用开发板 **3.3V**。
- 四个舵机与 PCA9685 的 **V+ / 舵机电源** 必须接 **5V 大电流电源**（建议 ≥2A）。
- **GND 必须共地**：ESP32 GND、PCA9685 GND、舵机电源 GND、音频模块 GND 全部连在一起。

---

## 2. ESP32-S3 引脚分配表

以下引脚与当前 `config.h` 一致。若你实际接线不同，请只改 `config.h` 后重新编译，不要改多处代码。

### 2.1 音频模块（CLK / DATA / LRCLK / BCLK / SDATA / 5V / GND）

该模块**不是**一根 I2S 总线：麦克为 **PDM（CLK+DATA）**，功放为 **I2S（LRCLK+BCLK+SDATA）**。  
`CLK` 必须接线，悬空会导致 `mic level peak=0`。

| 模块引脚 | 功能 | ESP32-S3 |
|----------|------|----------|
| **CLK** | 麦克 PDM 时钟 | **GPIO 4** |
| **DATA** | 麦克 PDM 数据 | **GPIO 5** |
| **BCLK** | I2S 位时钟 | **GPIO 7** |
| **LRCLK** | 功放帧时钟 | **GPIO 15** |
| **SDATA** | 功放音频输入 | **GPIO 6** |
| **5V / VCC** | 电源 | 3.3V 或 5V（模块说明可用 3.3V） |
| **GND** | 地 | GND |

```
  模块              ESP32-S3
  CLK   ──────────► GPIO 4
  DATA  ──────────► GPIO 5
  BCLK  ──────────► GPIO 7
  LRCLK ──────────► GPIO 15
  SDATA ──────────► GPIO 6
  GND / VCC
```

> 固件 `config.h` 中 `AUDIO_I2S_USE_PDM_MIC=1`。串口应出现 `Simplex channels created`（PDM 麦克 + I2S 喇叭）。  
> **SDATA** 不是 PCA9685 的 I2C SDA（GPIO 10）。

### 2.2 ST7789 240×240 屏幕（SPI）

| 功能 | ESP32-S3 GPIO | 接屏幕 |
|------|---------------|--------|
| MOSI / SDA | **GPIO 47** | SDA / MOSI |
| SCLK / SCK | **GPIO 21** | SCL / SCK |
| DC | **GPIO 40** | DC |
| RST | **GPIO 45** | RST |
| CS | **GPIO 41** | CS |
| 背光 BL | **GPIO 42** | BL / LED |
| VCC | 3.3V | VCC |
| GND | GND | GND |

> 部分 7 针模块无 CS，可将屏幕 CS 接 GND 或在硬件上常选通；固件仍使用 GPIO 41 作为 CS。  
> menuconfig 中 LCD 类型须选 **「ST7789 240*240」**。

### 2.3 PCA9685 舵机驱动板（6 针：GND / OE / SCL / SDA / VCC / V+）

板上只有这 6 个焊盘时，按下面**一一对应**即可（与音频模块的 SDA 无关）：

| PCA9685 引脚 | 接到哪里 |
|--------------|----------|
| **GND** | ESP32 **GND**（并与 5V 舵机电源负极共地） |
| **OE** | ESP32 **GPIO 8**（由固件控制；见下方说明） |
| **SCL** | ESP32 **GPIO 9** |
| **SDA** | ESP32 **GPIO 10** |
| **VCC** | ESP32 **3.3V**（PCA9685 逻辑电） |
| **V+** | 外部 **5V 电源正极**（只给舵机供电，≥2A 推荐） |

**OE 引脚说明（重要）**

- PCA9685 的 **OE 低电平有效**：OE 为低时 PWM 输出到舵机，为高时输出高阻（无 PWM）。
- 固件默认：**GPIO 8 高 = 关断 PWM**，**GPIO 8 低 = 允许转动**。
- 上电后 ESP 会**第一时间**拉高 GPIO 8，避免启动阶段舵机乱转；仅在小智调用 `self.servo.move` 等工具时才短暂拉低 OE。
- **不推荐**把 OE 直接接 GND（会一直允许输出，上电易转）；若必须接 GND，请在 `config.h` 设 `SERVO_OE_PIN_NUM (-1)` 并设 `SERVO_LAZY_INIT 0`。

```
  PCA9685 模块              ESP32-S3 / 电源
  ─────────────            ───────────────
  GND  ─────────────────── GND
  OE   ─────────────────── GPIO 8
  SCL  ─────────────────── GPIO 9
  SDA  ─────────────────── GPIO 10
  VCC  ─────────────────── 3.3V
  V+   ─────────────────── 5V 舵机专用电源 +
```

舵机信号线插在驱动板 **0 / 1 / 2 / 3** 号插座（左前 / 右前 / 左后 / 右后）。  
舵机 **红线 → 5V（V+ 同源）**，**棕黑线 → GND**，橙黄信号线 → 对应通道。

默认 I2C 地址：**0x40**（地址跳线 A0~A5 未焊接时）。

### 2.4 四个 MG90S 360° 舵机

| 舵机位置 | 代号 | PCA9685 通道 | 说明 |
|----------|------|--------------|------|
| 左前 | FL | **0** | `SERVO_CHANNEL_FL` |
| 右前 | FR | **1** | `SERVO_CHANNEL_FR` |
| 左后 | RL | **2** | `SERVO_CHANNEL_RL` |
| 右后 | RR | **3** | `SERVO_CHANNEL_RR` |

每路舵机三根线：

| 线色（常见） | 接法 |
|--------------|------|
| 棕 / 黑 | GND（与 PCA9685 / 5V 电源共地） |
| 红 | 5V（接舵机电源，不要从 ESP32 5V 脚硬拖 4 路） |
| 橙 / 黄 / 白 | 信号 → PCA9685 对应通道插座 |

推荐小车布局（俯视）：

```
        车头 ↑
    [FL]     [FR]
    [RL]     [RR]
```

### 2.5 其它

| 功能 | GPIO |
|------|------|
| PCA9685 OE（PWM 输出使能） | **GPIO 8** |
| PCA9685 I2C SCL | **GPIO 9** |
| PCA9685 I2C SDA | **GPIO 10** |
| 板载 LED | GPIO 48 |
| BOOT 按键（配网 / 对话） | GPIO 0 |
| 测试用灯（可选） | GPIO 18 |

---

## 3. 编译前准备

### 3.1 软件环境

- 安装 [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **5.4 或以上**（推荐 5.4 / 5.5）。
- 或使用 VSCode / Cursor 的 **ESP-IDF 插件**。
- 将本仓库克隆到本地：

```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
```

### 3.2 加载 ESP-IDF 环境

Linux / macOS：

```bash
source ~/esp/esp-idf/export.sh
```

Windows 使用 **「ESP-IDF 5.x CMD」** 或 PowerShell 中 `export.ps1`。

---

## 4. menuconfig 配置

在项目根目录执行：

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

在菜单中设置：

| 菜单路径 | 选项 |
|----------|------|
| **Xiaozhi Assistant → Board Type** | `面包板 ESP32-S3 + LCD`（`BREAD_COMPACT_WIFI_LCD`） |
| **Xiaozhi Assistant → LCD Type** | `ST7789 240*240` |
| **Partition Table**（如首次编译） | 使用工程默认 v2 分区（参见 `partitions/v2/README.md`） |

保存退出（`S` 保存，`Q` 退出）。

---

## 5. 编译与烧录

### 5.1 编译

```bash
cd xiaozhi-esp32
idf.py build
```

### 5.2 烧录

USB 连接 ESP32-S3 后，查看串口（macOS 多为 `/dev/cu.usbmodem*`，Linux 多为 `/dev/ttyACM0`）：

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

### 5.3 查看日志

```bash
idf.py -p /dev/cu.usbmodemXXXX monitor
```

退出监视：`Ctrl + ]`。

### 5.4 一键编译并烧录

```bash
idf.py -p /dev/cu.usbmodemXXXX build flash monitor
```

> 首次使用或无法连接 Wi-Fi：按住 **BOOT（GPIO 0）** 上电或复位，进入配网模式，用手机连接设备热点按提示配置。  
> 也可参考官方文档：[新手烧录固件教程](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)。

---

## 6. 语音控制舵机（MCP）

设备联网并连接小智服务器后，可直接用中文对话，例如：

| 说法 | 固件行为 |
|------|----------|
| 「前进」 / 「向前走」 | 默认速度前进（约 50/100） |
| 「慢速前进」 / 「快速后退」 | `move` 的 `speed_level` 或 action 含快慢前缀 |
| 「全速前进」 | 速度约 100 |
| 「快点」 / 「再慢一点」 | `self.servo.change_speed`（`delta` ±15~25） |
| 「把默认速度调到 30」 | `self.servo.set_default_speed` |
| 「后退」 / 「左转」 / 「右转」 | 同向，可带速度 |
| 「边走边向左拐」 / 「前进右转」 | `move` 的 `turn`（-100~100）走弧线，负=左、正=右 |
| 「停下」 | 平滑减速到停并关断 OE |
| 「向前走 3 秒」 | `duration_ms`≈3000 后自动平滑停 |

**平滑加减速（斜坡）**：起步、停车、换向时速度会在约 250ms（默认）内渐变，减小电流冲击与车身顿挫。
由 `SERVO_RAMP_ENABLED` / `SERVO_RAMP_STEP` / `SERVO_RAMP_TICK_MS` 控制；设 `SERVO_RAMP_ENABLED 0` 可回到瞬间到位的旧行为。
紧急截停请用 `self.servo.stop`（不走减速，立即关断）。

**速度档位（`speed_level` 或 `speed` 1~100）**

| 档位 | speed_level | 约 speed 值 |
|------|-------------|-------------|
| 龟速 | crawl / 龟速 | 8 |
| 慢 | slow / 慢速 | 18 |
| 中 | medium / 中速 | 30（默认） |
| 快 | fast / 快速 | 55 |
| 全速 | max / 全速 | 100 |

脉宽在 `[SERVO_SPEED_MIN_OFFSET_US, SERVO_SPEED_MAX_OFFSET_US]`（默认 **200~350µs**）间按速度线性插值：
speed=1≈200µs，speed=100≈350µs。

> **为什么 MIN 不是 0**：连续旋转舵机有较大「启动死区」——偏移太小时四轮会同向转（各自跨不过启动点），
> 全速才协调前进。用 `MIN` 做死区补偿，让低速也直接越过死区，方向才稳。
> 低速仍方向不对/不动 → 调大 `MIN`；低速太冲 → 调小 `MIN`；整体太快 → 减小 `MAX`。
> 可选 `SERVO_USE_QUADRATIC_SPEED 1` 让低速更柔。

云端通过 MCP 调用下方工具。`get_device_status` 会返回 `mobile_robot` 字段，标明本机为四轮连续旋转舵机小车。

### 6.1 已注册的 MCP 工具

| 工具名 | 作用 |
|--------|------|
| `self.servo.move` | 整车运动；`speed` 1~100（0=默认）；`speed_level` 慢/快/全速；`turn` -100~100 弧线转向；action 可写「慢速前进」「前进左转」；自动平滑加减速 |
| `self.servo.change_speed` | 行驶中加减速 `delta`（如 +20 更快，-15 更慢） |
| `self.servo.set_default_speed` | 设置默认速度 1~100 |
| `self.servo.set_speed` | 单轮调速，`servo`=fl/fr/rl/rr，`speed`=-100~100 |
| `self.servo.set_all` | 同时设置四轮速度 |
| `self.servo.stop` | 立即停止并关断 PWM |
| `self.servo.set_gain` | 校正左右轮速差走直线；`servo`=left/right/all/fl/fr/rl/rr，`gain`=10~150%（右快往左偏就把 right 调小），即时生效 |
| `self.servo.set_stop_pulse` | 逐轮校准 360° 舵机停转脉宽；`servo`=all/fl/fr/rl/rr（OE 保持开便于观察） |
| `self.servo.get_state` | 查询四轮速度、默认速度、是否在动 |

更多 MCP 说明见 [mcp-usage_zh.md](./mcp-usage_zh.md)。

### 6.2 启动日志（正常时应看到）

```
I CompactWifiBoardLCD: PCA9685 OE disabled early (GPIO8 high)
I CompactWifiBoardLCD: Servo I2C bus ready (SDA=10 SCL=9)
I MCP: Add tool: self.servo.move
I Pca9685Servo: lazy init: OE off (GPIO8), PWM on first voice command
I CompactWifiBoardLCD: PCA9685 servo MCP registered (addr=0x40, lazy=1)
```

对话说「前进」时，串口应出现 `MCP: tools/call: self.servo.move` 及 `Pca9685Servo: move ...`。

### 6.3 重要说明：360° 连续旋转舵机

MG90S **360° 版**是**调速电机**，不是 0°~180° 定位舵机：

- `speed = 0` / 「停下」→ 停止：固件靠**拉高 OE 切断 PWM 输出**让舵机断电停转，而不是依赖某个“停止脉宽”。
  这很关键——廉价连续舵机常常没有一个能让它精确停住的脉宽，但只要 OE 接好，「停下」就能可靠停车。
- `move` 的 `speed` 1~100 → 只表示**转速快慢**，不是角度；越大 PWM 偏离停止脉宽越多，转得越快
- 调速参数见 `SERVO_SPEED_MIN_OFFSET_US`、`SERVO_SPEED_MAX_OFFSET_US`、`SERVO_USE_QUADRATIC_SPEED`（见 §6 速度档位表下说明）
- **停转点不一致**：若四个舵机一致性差，可用 `self.servo.set_stop_pulse`（servo=fl/fr/rl/rr）逐轮校准（OE 保持开、持续输出便于观察），
  再把值填进 `SERVO_STOP_US_FL`~`RR`。不过日常停车靠 OE，通常无需精确校准。

**左右镜像安装(重要)**：四轮小车左右两侧舵机为镜像安装，同一 PWM 会使左右轮物理转向相反。
因此固件默认把**右侧两轮(FR/RR)反向**，使「前进」时整车真正向前：

```c
#define SERVO_FL_INVERT  0
#define SERVO_FR_INVERT  1   // 右前默认反向
#define SERVO_RL_INVERT  0
#define SERVO_RR_INVERT  1   // 右后默认反向
```

实测调试：
- 整车前后方向反了 → 改 `SERVO_SPEED_SIGN`（`1` ↔ `-1`）。
- 某一侧两轮同时反 → 把那一侧的两个 `*_INVERT` 都取反（左右各管一边）。
- 只有单个轮子反 → 只改它对应的 `*_INVERT`。

改后需重新 `idf.py build flash`。

---

## 7. 常见问题

### 7.0 插上电「完全没反应」（先只调试屏幕 + 音频）

按下面顺序排查，**不要先接 PCA9685**（未接好时 I2C 初始化可能让板子反复重启）。

1. **先确认已烧录本工程固件**（出厂固件或没烧录时，屏幕/语音都不会有小智界面）。  
2. **打开串口监视**（见上文 `idf.py -p /dev/cu.usbmodem101 monitor`），上电应看到 `ESP-ROM`、Wi-Fi 等日志；若不断 `Guru Meditation` / `abort`，把 PCA9685 的 SDA/SCL 杜邦线拔掉再试。  
3. **板载 LED（GPIO 48）** 是否闪：有闪说明程序在跑，问题多在屏幕接线或背光。  
4. **屏幕背光 BL → GPIO 42**：背光没接会「黑屏但程序正常」。  
5. **menuconfig**：板型 `面包板 ESP32-S3 + LCD`，LCD **ST7789 240×240**。  
6. **只接 USB 数据线**（不少线只能充电）。  
7. 若暂时不接舵机，可在 `config.h` 将 `SERVO_PCA9685_ENABLED` 设为 `0` 后重新编译烧录。

**当前屏幕 + 音频（PDM 麦 + I2S 喇叭）接线见 §2.1**，屏幕见 §2.2。

### 7.1 上电舵机就转

1. 确认 **OE 接 GPIO 8**，不要悬空或只接 GND（除非你知道如何改 `SERVO_OE_PIN_NUM`）。  
2. 烧录**本仓库最新固件**，串口应有 `PCA9685 OE disabled early (GPIO8 high)`。  
3. `SERVO_LAZY_INIT` 建议为 `1`：上电不写 PWM，仅关 OE。  
4. 若仍转：用万用表量 GPIO 8 上电后是否为 **3.3V**；调 `SERVO_PULSE_STOP_US`（1480~1520）或语音后调用 `self.servo.set_stop_pulse`。

### 7.2 舵机不动 / I2C 失败

1. 检查 **5V 舵机电源**是否接通、电流是否足够。  
2. I2C：**SDA=GPIO10，SCL=GPIO9**，与 `config.h` 一致。  
3. 串口是否有 `PCA9685 not detected at 0x40`；若有，查接线与共地。  
4. 对话时是否出现 `tools/call: self.servo.move`；若无，见 §7.7。

### 7.3 方向不对 / 只有部分轮子转

- 在 `config.h` 调整 `SERVO_FL_INVERT` 等四个宏。  
- 确认舵机插在通道 **0~3**，与 `SERVO_CHANNEL_*` 一致。

### 7.4 屏幕不亮或花屏

- menuconfig 是否选了 **ST7789 240×240**。  
- 核对 SPI 线序，尤其 **MOSI=47、SCK=21**。

### 7.5 麦克风 peak=0

1. 按 §2.1 接线：CLK→4，DATA→5，SDATA→6，BCLK→7，LRCLK→15。  
2. 固件为 **PDM 麦克 + I2S 喇叭**，串口应有 `Simplex channels created`。  
3. 引脚在 `config.h` 的 `AUDIO_I2S_MIC_PDM_*` / `AUDIO_I2S_SPK_GPIO_*` 中，改线后需 `idf.py build flash`。

### 7.6 无声音或无法唤醒（喇叭也不响）

- 核对 §2.1 四线接线；**VCC = 3.3V**。

### 7.7 小智说「不是小车」/ 不调用舵机 MCP

- 确认串口已注册 `self.servo.move` 等工具（见 §6.2）。  
- 设备需联网；对话时应出现 `MCP: tools/call: self.servo.move`。  
- 可说「查设备状态」：返回 JSON 中应有 `mobile_robot`（`move_tool`: `self.servo.move`）。  
- 若工具已注册但仍不调用，多为云端模型未选用工具；可明确说「用小车工具前进」或更新固件（工具描述已强调本机为四轮小车）。

---

## 8. 自定义引脚（修改 config.h）

文件路径：`main/boards/bread-compact-wifi-lcd/config.h`

| 宏 | 含义 |
|----|------|
| `AUDIO_I2S_USE_PDM_MIC` | `1`=PDM 麦克 + I2S 喇叭（当前默认） |
| `AUDIO_I2S_MIC_PDM_*` / `AUDIO_I2S_SPK_GPIO_*` | 音频引脚，见 §2.1 |
| `SERVO_PCA9685_ENABLED` | `1` 启用舵机；`0` 关闭 I2C/MCP |
| `SERVO_I2C_SDA_PIN` / `SERVO_I2C_SCL_PIN` | PCA9685 I2C（默认 10 / 9） |
| `SERVO_PCA9685_ADDR` | I2C 地址（默认 0x40） |
| `SERVO_OE_PIN_NUM` | OE 接的 GPIO 编号（默认 8）；不接 OE 填 `-1` |
| `SERVO_OE_GPIO` | 由 `SERVO_OE_PIN_NUM` 推导，一般勿改 |
| `SERVO_LAZY_INIT` | `1` 上电只关 OE，首次语音再 Init |
| `SERVO_PULSE_STOP_US` | 360° 舵机停止脉宽（默认 1500） |
| `SERVO_SPEED_SIGN` | 前进方向符号，`1` 或 `-1` |
| `SERVO_SPEED_MIN_OFFSET_US` | speed=1 时偏移=死区补偿（默认 200，低速跨不过死区就调大） |
| `SERVO_SPEED_MAX_OFFSET_US` | speed=100 时最大脉宽偏移（默认 350，越小越慢） |
| `SERVO_USE_QUADRATIC_SPEED` | `1` 二次曲线（低速更细腻） |
| `SERVO_STOP_US_FL`~`RR` | 每轮独立停转脉宽（默认 0=用全局；连续舵机停转点不一致时逐轮校准后填） |
| `SERVO_GAIN_FL`~`RR` | 每轮速度增益%（补偿左右速度差走直线，默认右侧 FR/RR=65；用 set_gain 调好后填） |
| `SERVO_DEFAULT_SPEED` | 默认行驶速度 1~100（默认 30） |
| `SERVO_RAMP_ENABLED` | `1` 平滑加减速；`0` 瞬间到位（旧行为） |
| `SERVO_RAMP_TICK_MS` | 斜坡更新周期 ms（默认 20） |
| `SERVO_RAMP_STEP` | 每周期速度变化上限 1~100（默认 8，越小越柔；0→100 用时≈100/STEP×TICK） |
| `SERVO_TURN_GAIN` | 弧线转向强度 %（默认 100，越大转弯越急） |
| `SERVO_STOP_MODE` | 停止写寄存器次数，`1` 推荐 |
| `SERVO_CHANNEL_FL` ~ `RR` | 舵机通道 0~15 |
| `SERVO_FL_INVERT` ~ `RR` | 单轮反向 0/1 |

修改后执行：

```bash
idf.py build flash
```

---

## 9. 相关文档

- [小智 AI 百科全书（面包板专题）](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb?from=from_copylink)  
- [MCP 物联网控制用法](./mcp-usage_zh.md)  
- [自定义开发板指南](./custom-board_zh.md)  
- [分区表说明](../partitions/v2/README.md)

---

*文档版本：与 `bread-compact-wifi-lcd` + PCA9685 四轮小车固件配套（OE=8 SCL=9 SDA=10，MCP `self.servo.*`）。*
