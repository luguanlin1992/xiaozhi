#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "bread_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "pca9685_servo_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <cJSON.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardLCD"

class CompactWifiBoardLCD : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
#if SERVO_PCA9685_ENABLED
    i2c_master_bus_handle_t servo_i2c_bus_ = nullptr;
#endif

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

#if SERVO_PCA9685_ENABLED
    // 上电第一时间拉高 OE，避免 ESP 启动前 PCA9685 默认 PWM 导致舵机乱转
    static void DisableServoPwmOutputsEarly() {
#if SERVO_OE_PIN_NUM >= 0
        gpio_reset_pin(SERVO_OE_GPIO);
        gpio_set_direction(SERVO_OE_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(SERVO_OE_GPIO, GPIO_PULLUP_ONLY);
        gpio_set_level(SERVO_OE_GPIO, 1);
        ESP_LOGI(TAG, "PCA9685 OE disabled early (GPIO%d high)", SERVO_OE_PIN_NUM);
#endif
    }

    void InitializeServoOePinEarly() {
        DisableServoPwmOutputsEarly();
    }

    void InitializeServoI2c() {
        if (SERVO_I2C_SDA_PIN == GPIO_NUM_NC || SERVO_I2C_SCL_PIN == GPIO_NUM_NC) {
            return;
        }
        InitializeServoOePinEarly();
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = SERVO_I2C_SDA_PIN,
            .scl_io_num = SERVO_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &servo_i2c_bus_));
        ESP_LOGI(TAG, "Servo I2C bus ready (SDA=%d SCL=%d)", SERVO_I2C_SDA_PIN, SERVO_I2C_SCL_PIN);
    }
#endif

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
#if SERVO_PCA9685_ENABLED
        if (servo_i2c_bus_ != nullptr) {
            esp_err_t probe_err =
                i2c_master_probe(servo_i2c_bus_, SERVO_PCA9685_ADDR, 200);
            if (probe_err != ESP_OK) {
                ESP_LOGW(TAG,
                         "PCA9685 not detected at 0x%02x (%s); servo disabled. "
                         "Check SDA=GPIO%d SCL=GPIO%d, OE->GPIO%d, VCC=3.3V",
                         SERVO_PCA9685_ADDR, esp_err_to_name(probe_err), SERVO_I2C_SDA_PIN,
                         SERVO_I2C_SCL_PIN, SERVO_OE_PIN_NUM);
                return;
            }
            static const Pca9685ServoConfig servo_config = [] {
                Pca9685ServoConfig cfg;
                cfg.channels = {SERVO_CHANNEL_FL, SERVO_CHANNEL_FR, SERVO_CHANNEL_RL, SERVO_CHANNEL_RR};
                cfg.invert = {SERVO_FL_INVERT, SERVO_FR_INVERT, SERVO_RL_INVERT, SERVO_RR_INVERT};
                cfg.oe_pin = SERVO_OE_PIN_NUM;
                cfg.stop_pulse_us = SERVO_PULSE_STOP_US;
                cfg.stop_pulse_per_wheel = {SERVO_STOP_US_FL, SERVO_STOP_US_FR, SERVO_STOP_US_RL,
                                            SERVO_STOP_US_RR};
                cfg.stop_mode = SERVO_STOP_MODE;
                cfg.speed_sign = SERVO_SPEED_SIGN;
                cfg.speed_min_offset_us = SERVO_SPEED_MIN_OFFSET_US;
                cfg.speed_max_offset_us = SERVO_SPEED_MAX_OFFSET_US;
                cfg.quadratic_speed = SERVO_USE_QUADRATIC_SPEED;
                cfg.lazy_init = SERVO_LAZY_INIT;
                cfg.default_speed = SERVO_DEFAULT_SPEED;
                cfg.ramp_enabled = SERVO_RAMP_ENABLED;
                cfg.ramp_tick_ms = SERVO_RAMP_TICK_MS;
                cfg.ramp_step = SERVO_RAMP_STEP;
                cfg.turn_gain = SERVO_TURN_GAIN;
                cfg.wheel_gain = {SERVO_GAIN_FL, SERVO_GAIN_FR, SERVO_GAIN_RL, SERVO_GAIN_RR};
                return cfg;
            }();
            static Pca9685ServoController servo_controller(servo_i2c_bus_, SERVO_PCA9685_ADDR,
                                                           SERVO_PWM_FREQUENCY_HZ, servo_config);
            ESP_LOGI(TAG, "PCA9685 servo MCP registered (addr=0x%02x, lazy=%d)", SERVO_PCA9685_ADDR,
                     SERVO_LAZY_INIT);
        }
#endif
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO) {
#if SERVO_PCA9685_ENABLED
        DisableServoPwmOutputsEarly();
#endif
        InitializeSpi();
        InitializeLcdDisplay();
#if SERVO_PCA9685_ENABLED
        InitializeServoI2c();
#endif
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#if AUDIO_I2S_USE_PDM_MIC
        static NoAudioCodecSimplexPdm audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_PDM_CLK, AUDIO_I2S_MIC_PDM_DIN);
#elif defined(AUDIO_I2S_METHOD_SIMPLEX)
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static BreadAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

#if SERVO_PCA9685_ENABLED
    virtual std::string GetDeviceStatusJson() override {
        std::string base = WifiBoard::GetDeviceStatusJson();
        cJSON* root = cJSON_Parse(base.c_str());
        if (root == nullptr) {
            return base;
        }
        auto robot = cJSON_CreateObject();
        cJSON_AddStringToObject(robot, "type", "4wd_continuous_servo");
        cJSON_AddStringToObject(robot, "move_tool", "self.servo.move");
        cJSON_AddStringToObject(robot, "stop_tool", "self.servo.stop");
        cJSON_AddBoolToObject(robot, "i2c_ready", servo_i2c_bus_ != nullptr);
        cJSON_AddStringToObject(robot, "speed_tool", "self.servo.change_speed");
        cJSON_AddStringToObject(robot, "hint",
                                "Move: self.servo.move with speed 1-100 or speed_level slow/fast. "
                                "Adjust: self.servo.change_speed delta. 前进/后退/快一点/慢速前进");
        cJSON_AddItemToObject(root, "mobile_robot", robot);

        char* printed = cJSON_PrintUnformatted(root);
        std::string result(printed != nullptr ? printed : base);
        if (printed != nullptr) {
            cJSON_free(printed);
        }
        cJSON_Delete(root);
        return result;
    }
#endif
};

DECLARE_BOARD(CompactWifiBoardLCD);
