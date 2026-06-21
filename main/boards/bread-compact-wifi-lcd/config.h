#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 模块引脚：麦克 PDM(CLK+DATA) + 功放 I2S(LRCLK+BCLK+SDATA)，与 Duplex 单总线不兼容
#define AUDIO_I2S_USE_PDM_MIC    1

#if AUDIO_I2S_USE_PDM_MIC

#define AUDIO_I2S_MIC_PDM_CLK   GPIO_NUM_4   // 模块 CLK
#define AUDIO_I2S_MIC_PDM_DIN   GPIO_NUM_5   // 模块 DATA
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_7   // 模块 BCLK
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_15  // 模块 LRCLK
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_6   // 模块 SDATA

#else

// #define AUDIO_I2S_METHOD_SIMPLEX
#ifdef AUDIO_I2S_METHOD_SIMPLEX
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_7
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_6
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_4
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_5
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16
#else
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_7
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_4
#define AUDIO_I2S_MIC_RX_SLOT_RIGHT  0
#endif

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC


#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_42
#define DISPLAY_MOSI_PIN      GPIO_NUM_47
#define DISPLAY_CLK_PIN       GPIO_NUM_21
#define DISPLAY_DC_PIN        GPIO_NUM_40
#define DISPLAY_RST_PIN       GPIO_NUM_45
#define DISPLAY_CS_PIN        GPIO_NUM_41


#ifdef CONFIG_LCD_ST7789_240X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X320_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_170X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_172X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X280
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240_7PIN
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

#ifdef CONFIG_LCD_ST7789_240X135
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X160
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X128
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320_NO_IPS
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_GC9A01_240X240
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_CUSTOM
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

// ── PCA9685 + 4×MG90S 360° 连续旋转舵机（四轮小车）────────────────────────
// 设为 0 可完全关闭舵机 I2C/MCP，便于先调屏幕与音频
#define SERVO_PCA9685_ENABLED   1
#define SERVO_I2C_SCL_PIN       GPIO_NUM_9
#define SERVO_I2C_SDA_PIN       GPIO_NUM_10
#define SERVO_PCA9685_ADDR      0x40
#define SERVO_PWM_FREQUENCY_HZ  50.0f

// 停止脉宽(us)。360° 舵机 speed=0 时输出此脉宽；若停不住可试 1480~1520
#define SERVO_PULSE_STOP_US     1500

// 每轮独立停转脉宽(0=沿用上面的全局值)。连续舵机每个停转点常有差异，
// 用语音 self.servo.set_stop_pulse 逐轮校准出停转点后，把数值填到这里固化。
#define SERVO_STOP_US_FL        0
#define SERVO_STOP_US_FR        0
#define SERVO_STOP_US_RL        0
#define SERVO_STOP_US_RR        0

// 前进方向反了改 SIGN 为 1 或 -1
#define SERVO_SPEED_SIGN        (-1)
// speed 1~100 → 脉宽偏移(us)，在 [MIN, MAX] 间按速度插值。
// 这批连续旋转舵机启动死区较大：偏移太小时四轮会同向(跨不过各自启动点)，全速(300)才协调前进。
// 故用 MIN 做“死区补偿”，让低速也直接越过死区：speed1≈MIN，speed100≈MAX。
// 若低速仍方向不对/不动 → 调大 MIN；若低速太冲 → 调小 MIN。
#define SERVO_SPEED_MIN_OFFSET_US 200
#define SERVO_SPEED_MAX_OFFSET_US 350
#define SERVO_USE_QUADRATIC_SPEED 0
// 二次曲线（USE=1）：低速更柔，但 speed<15 可能几乎不动
#define SERVO_SPEED_STEP_US     1

// 默认行驶速度(1~100)：move 未指定 speed/speed_level 时使用
#define SERVO_DEFAULT_SPEED     30

// ── 平滑加减速(斜坡) ───────────────────────────────────────────────
// 1=起步/停车/换向时速度渐变，减小电流冲击与顿挫；0=瞬间到位(旧行为)
#define SERVO_RAMP_ENABLED      1
// 斜坡更新周期(ms)。20ms≈50Hz，与舵机刷新率匹配
#define SERVO_RAMP_TICK_MS      20
// 每个周期速度变化上限(1~100)。0→100 用时≈(100/STEP)×TICK_MS；STEP=8 约 250ms
#define SERVO_RAMP_STEP         8

// ── 弧线转向 ───────────────────────────────────────────────────────
// move 的 turn(-100~100) 强度(%)：100=turn 满量时内侧轮反转可原地打转；调小则转弯更缓
#define SERVO_TURN_GAIN         100

// 停止时写寄存器次数：0=1 次，1=连写 5 次(推荐)。勿用 (on,off)=(0,0) 关 PWM
#define SERVO_STOP_MODE         1

// 1=上电仅关 OE、首次语音再 Init PCA9685；0=上电即写停止脉宽
#define SERVO_LAZY_INIT         1

// OE 接 ESP32 时用整数引脚号（#if 不能用 GPIO_NUM_* 枚举）。不接 OE 填 -1
// PCA9685 OE 低有效：GPIO 高=无 PWM，低=允许输出
#define SERVO_OE_PIN_NUM        8
#define SERVO_OE_GPIO           GPIO_NUM_8

// 舵机信号接 PCA9685 通道 0~15（俯视：左前/右前/左后/右后）
#define SERVO_CHANNEL_FL        0
#define SERVO_CHANNEL_FR        1
#define SERVO_CHANNEL_RL        2
#define SERVO_CHANNEL_RR        3

// 每轮速度增益(%)：补偿左右舵机速度差走直线。100=不变；右轮偏快→把 FR/RR 调小(如 65)。
// 可先用语音 self.servo.set_gain 实时调出合适值，再填到这里固化。
#define SERVO_GAIN_FL           100
#define SERVO_GAIN_FR           65
#define SERVO_GAIN_RL           100
#define SERVO_GAIN_RR           65

// 单轮转向反了：对应项改为 1
// 四轮小车左右舵机为镜像安装：同一 PWM 会使左右轮物理转向相反，
// 故默认把右侧两轮(FR/RR)反向，使「前进」时整车真正向前。
// 若实测发现是左侧反了或整车方向不对，改这四项或 SERVO_SPEED_SIGN。
#define SERVO_FL_INVERT         0
#define SERVO_FR_INVERT         1
#define SERVO_RL_INVERT         0
#define SERVO_RR_INVERT         1

#endif // _BOARD_CONFIG_H_
