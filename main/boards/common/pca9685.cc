#include "pca9685.h"

#include <cmath>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Pca9685"

#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06

#define PCA9685_MODE1_AI 0x20        // 必须开启，否则单次 I2C 写 4 字节 PWM 只有 ON_L 生效
#define PCA9685_MODE1_SLEEP 0x10
#define PCA9685_MODE1_RESTART 0x80
#define PCA9685_MODE2_OUTDRV 0x04

#define PCA9685_I2C_TIMEOUT_MS 250

static constexpr float kOscillatorHz = 25000000.0f;
static constexpr uint16_t kPwmResolution = 4096;
static constexpr int kI2cRetryCount = 3;

Pca9685::Pca9685(i2c_master_bus_handle_t i2c_bus, uint8_t addr, float pwm_frequency_hz)
    : I2cDevice(i2c_bus, addr, 100000), pwm_frequency_hz_(pwm_frequency_hz) {}

bool Pca9685::SetPwmEx(uint8_t channel, uint16_t on, uint16_t off) {
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    uint8_t buffer[5] = {
        reg,
        static_cast<uint8_t>(on & 0xFF),
        static_cast<uint8_t>(on >> 8),
        static_cast<uint8_t>(off & 0xFF),
        static_cast<uint8_t>(off >> 8),
    };
    for (int attempt = 0; attempt < kI2cRetryCount; ++attempt) {
        esp_err_t err = TransmitEx(buffer, sizeof(buffer), PCA9685_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "SetPwm ch=%u on=%u off=%u failed", channel, on, off);
    return false;
}

uint16_t Pca9685::PulseUsToTicks(uint16_t pulse_us) const {
    uint32_t period_us = static_cast<uint32_t>(1000000.0f / pwm_frequency_hz_);
    uint16_t ticks = static_cast<uint16_t>((static_cast<uint32_t>(pulse_us) * kPwmResolution) / period_us);
    if (ticks >= kPwmResolution) {
        ticks = kPwmResolution - 1;
    }
    return ticks;
}

bool Pca9685::Init(uint16_t stop_pulse_us, uint8_t stop_mode) {
    if (WriteRegEx(PCA9685_MODE1, PCA9685_MODE1_AI) != ESP_OK) {
        return false;
    }
    if (WriteRegEx(PCA9685_MODE2, PCA9685_MODE2_OUTDRV) != ESP_OK) {
        return false;
    }
    if (!SetPwmFrequencyEx(pwm_frequency_hz_)) {
        return false;
    }

    uint8_t mode1 = 0;
    if (ReadRegEx(PCA9685_MODE1, &mode1) != ESP_OK) {
        return false;
    }
    if (WriteRegEx(PCA9685_MODE1, (mode1 & ~PCA9685_MODE1_SLEEP) | PCA9685_MODE1_AI | PCA9685_MODE1_RESTART) !=
        ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    initialized_ = true;
    if (!ApplyStopOutputs(stop_pulse_us, stop_mode)) {
        ESP_LOGE(TAG, "ApplyStopOutputs failed");
        initialized_ = false;
        return false;
    }
    ESP_LOGI(TAG, "init ok, %.1f Hz, stop %uus mode %u", pwm_frequency_hz_, stop_pulse_us, stop_mode);
    return true;
}

bool Pca9685::SetPwmFrequencyEx(float frequency_hz) {
    float prescale_val = std::floor(kOscillatorHz / (kPwmResolution * frequency_hz)) - 1.0f;
    if (prescale_val < 3.0f) {
        prescale_val = 3.0f;
    }
    if (prescale_val > 255.0f) {
        prescale_val = 255.0f;
    }

    uint8_t mode1 = 0;
    if (ReadRegEx(PCA9685_MODE1, &mode1) != ESP_OK) {
        return false;
    }
    if (WriteRegEx(PCA9685_MODE1, (mode1 & ~PCA9685_MODE1_RESTART) | PCA9685_MODE1_AI | PCA9685_MODE1_SLEEP) !=
        ESP_OK) {
        return false;
    }
    if (WriteRegEx(PCA9685_PRESCALE, static_cast<uint8_t>(prescale_val)) != ESP_OK) {
        return false;
    }
    if (WriteRegEx(PCA9685_MODE1, (mode1 & ~PCA9685_MODE1_SLEEP) | PCA9685_MODE1_AI | PCA9685_MODE1_RESTART) !=
        ESP_OK) {
        return false;
    }
    pwm_frequency_hz_ = frequency_hz;
    return true;
}

void Pca9685::SetPwmFrequency(float frequency_hz) {
    ESP_ERROR_CHECK(SetPwmFrequencyEx(frequency_hz) ? ESP_OK : ESP_FAIL);
}

void Pca9685::SetPwm(uint8_t channel, uint16_t on, uint16_t off) {
    if (!initialized_) {
        return;
    }
    SetPwmEx(channel, on, off);
}

void Pca9685::SetPulseUs(uint8_t channel, uint16_t pulse_us) {
    if (!initialized_) {
        return;
    }
    SetPwm(channel, 0, PulseUsToTicks(pulse_us));
}

bool Pca9685::SetPulseUsRun(uint8_t start_channel, const uint16_t* pulse_us, uint8_t count) {
    if (!initialized_ || pulse_us == nullptr || count == 0) {
        return false;
    }
    if (static_cast<int>(start_channel) + count > 16) {
        return false;
    }
    // 寄存器从 LED0_ON_L 起按 (ON_L, ON_H, OFF_L, OFF_H) 每通道 4 字节连续排列，
    // MODE1.AI 已开启，故可一次写多通道；每通道 ON=0、OFF=ticks。
    uint8_t buffer[1 + 16 * 4];
    buffer[0] = PCA9685_LED0_ON_L + 4 * start_channel;
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t ticks = PulseUsToTicks(pulse_us[i]);
        uint8_t* p = &buffer[1 + i * 4];
        p[0] = 0;
        p[1] = 0;
        p[2] = static_cast<uint8_t>(ticks & 0xFF);
        p[3] = static_cast<uint8_t>(ticks >> 8);
    }
    const size_t len = 1 + static_cast<size_t>(count) * 4;
    for (int attempt = 0; attempt < kI2cRetryCount; ++attempt) {
        if (TransmitEx(buffer, len, PCA9685_I2C_TIMEOUT_MS) == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "SetPulseUsRun start=%u count=%u failed", start_channel, count);
    return false;
}

void Pca9685::LogChannelPwm(uint8_t channel, uint16_t expect_ticks) {
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    uint8_t data[4] = {};
    if (ReadRegEx(reg, &data[0]) != ESP_OK || ReadRegEx(reg + 1, &data[1]) != ESP_OK ||
        ReadRegEx(reg + 2, &data[2]) != ESP_OK || ReadRegEx(reg + 3, &data[3]) != ESP_OK) {
        ESP_LOGW(TAG, "ch%u pwm readback failed", channel);
        return;
    }
    uint16_t on = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    uint16_t off = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
    ESP_LOGI(TAG, "ch%u pwm readback on=%u off=%u (expect off=%u)", channel, on, off, expect_ticks);
}

bool Pca9685::ApplyStopOutputs(uint16_t stop_pulse_us, uint8_t stop_mode) {
    if (!initialized_) {
        return false;
    }

    // 360° 连续旋转舵机：切勿写 ON=0 OFF=0，无 PWM 时会一直转
    const int repeats = (stop_mode == 0) ? 1 : 5;
    const uint16_t ticks = PulseUsToTicks(stop_pulse_us);
    bool ok = true;

    for (int r = 0; r < repeats; ++r) {
        for (uint8_t ch = 0; ch < 16; ++ch) {
            if (!SetPwmEx(ch, 0, ticks)) {
                ok = false;
            }
        }
        if (r + 1 < repeats) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    LogChannelPwm(0, ticks);
    return ok;
}
