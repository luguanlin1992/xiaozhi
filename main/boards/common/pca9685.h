// PCA9685 I2C PWM 驱动；Init 须开启 MODE1.AI 位，否则多字节写 PWM 无效
#ifndef PCA9685_H
#define PCA9685_H

#include "i2c_device.h"

class Pca9685 : public I2cDevice {
public:
    Pca9685(i2c_master_bus_handle_t i2c_bus, uint8_t addr, float pwm_frequency_hz);

    // stop_mode: 0=写 1 次停止脉宽；1=连写 5 次(推荐)。360° 舵机勿用 (0,0) 关 PWM
    bool Init(uint16_t stop_pulse_us = 1500, uint8_t stop_mode = 1);

    void SetPwm(uint8_t channel, uint16_t on, uint16_t off);
    void SetPulseUs(uint8_t channel, uint16_t pulse_us);
    // 在一次 I2C 事务中写一段连续通道(start_channel..start_channel+count-1)的脉宽，count<=16。
    // 多个舵机一起更新时比逐通道 SetPulseUs 更快、更同步。
    bool SetPulseUsRun(uint8_t start_channel, const uint16_t* pulse_us, uint8_t count);
    bool ApplyStopOutputs(uint16_t stop_pulse_us, uint8_t stop_mode);

private:
    float pwm_frequency_hz_ = 50.0f;
    bool initialized_ = false;

    bool SetPwmEx(uint8_t channel, uint16_t on, uint16_t off);
    uint16_t PulseUsToTicks(uint16_t pulse_us) const;
    void LogChannelPwm(uint8_t channel, uint16_t expect_ticks);

    void SetPwmFrequency(float frequency_hz);
    bool SetPwmFrequencyEx(float frequency_hz);
};

#endif  // PCA9685_H
