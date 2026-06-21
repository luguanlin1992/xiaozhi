#ifndef BREAD_AUDIO_CODEC_H
#define BREAD_AUDIO_CODEC_H

#include "config.h"
#include "codecs/no_audio_codec.h"

#ifndef AUDIO_I2S_MIC_RX_SLOT_RIGHT
#define AUDIO_I2S_MIC_RX_SLOT_RIGHT 0
#endif

#include <algorithm>
#include <cstdint>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>

// 7 针一体模块：Duplex 单时钟；RX 实际采样率 = output_sample_rate（再重采样到 16k 给唤醒）
class BreadAudioCodecDuplex : public NoAudioCodec {
public:
    BreadAudioCodecDuplex(int input_sample_rate, int output_sample_rate, gpio_num_t bclk, gpio_num_t ws,
                          gpio_num_t dout, gpio_num_t din) {
        (void)input_sample_rate;
        duplex_ = true;
        output_sample_rate_ = output_sample_rate;
        // Duplex I2S 时钟按喇叭采样率运行，必须如实上报，AudioService 才会做 24k→16k 重采样
        input_sample_rate_ = output_sample_rate;

        i2s_chan_config_t chan_cfg = {
            .id = I2S_NUM_0,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
            .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
            .auto_clear_after_cb = true,
            .auto_clear_before_cb = false,
            .intr_priority = 0,
        };
        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

        i2s_std_config_t tx_cfg = MakeStdConfig(output_sample_rate_, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
        tx_cfg.gpio_cfg.bclk = bclk;
        tx_cfg.gpio_cfg.ws = ws;
        tx_cfg.gpio_cfg.dout = dout;
        tx_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_cfg));

#if AUDIO_I2S_MIC_RX_SLOT_RIGHT
        i2s_std_config_t rx_cfg = MakeStdConfig(output_sample_rate_, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_RIGHT);
#else
        i2s_std_config_t rx_cfg = MakeStdConfig(output_sample_rate_, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
#endif
        rx_cfg.gpio_cfg.bclk = bclk;
        rx_cfg.gpio_cfg.ws = ws;
        rx_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
        rx_cfg.gpio_cfg.din = din;
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &rx_cfg));

        ESP_LOGI(TAG, "Duplex in/out %dHz, mic DIN=%d, spk DOUT=%d", output_sample_rate_, din, dout);
    }

protected:
    int Read(int16_t* dest, int samples) override {
        size_t bytes_read;
#if AUDIO_I2S_MIC_RX_SLOT_RIGHT
        std::vector<int32_t> raw(samples);
        if (i2s_channel_read(rx_handle_, raw.data(), raw.size() * sizeof(int32_t), &bytes_read,
                             pdMS_TO_TICKS(200)) != ESP_OK) {
            return 0;
        }
        int frames = static_cast<int>(bytes_read / sizeof(int32_t));
        if (frames > samples) {
            frames = samples;
        }
        int peak = 0;
        for (int i = 0; i < frames; ++i) {
            int32_t v = raw[i] >> 12;
            if (v > INT16_MAX) {
                v = INT16_MAX;
            } else if (v < INT16_MIN) {
                v = INT16_MIN;
            }
            dest[i] = static_cast<int16_t>(v);
            peak = std::max(peak, std::abs(dest[i]));
        }
        LogMicPeak(peak);
        return frames;
#else
        std::vector<int32_t> raw(samples * 2);
        if (i2s_channel_read(rx_handle_, raw.data(), raw.size() * sizeof(int32_t), &bytes_read,
                             pdMS_TO_TICKS(200)) != ESP_OK) {
            return 0;
        }
        int frames = static_cast<int>(bytes_read / (sizeof(int32_t) * 2));
        if (frames > samples) {
            frames = samples;
        }
        int peak = 0;
        for (int i = 0; i < frames; ++i) {
            int16_t left = PcmFromRaw(raw[i * 2]);
            int16_t right = PcmFromRaw(raw[i * 2 + 1]);
            dest[i] = left;
            if (std::abs(right) > std::abs(left)) {
                dest[i] = right;
            }
            peak = std::max(peak, std::abs(dest[i]));
        }
        LogMicPeak(peak);
        return frames;
#endif
    }

private:
    static constexpr const char* TAG = "BreadAudio";

    static int16_t PcmFromRaw(int32_t raw) {
        int32_t v = raw >> 12;
        if (v > INT16_MAX) {
            return INT16_MAX;
        }
        if (v < INT16_MIN) {
            return INT16_MIN;
        }
        return static_cast<int16_t>(v);
    }

    static void LogMicPeak(int peak) {
        static int64_t last_us = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_us > 3000000) {
            ESP_LOGI(TAG, "mic level peak=%d (0≈无信号, 正常说话通常>500)", peak);
            last_us = now;
        }
    }

    static i2s_std_config_t MakeStdConfig(int sample_rate_hz, i2s_slot_mode_t slot_mode,
                                          i2s_std_slot_mask_t slot_mask) {
        i2s_std_config_t cfg = {
            .clk_cfg = {
                .sample_rate_hz = (uint32_t)sample_rate_hz,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
                .ext_clk_freq_hz = 0,
#endif
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = slot_mode,
                .slot_mask = slot_mask,
                .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
                .ws_pol = false,
                .bit_shift = true,
#ifdef I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
#endif
            },
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_GPIO_UNUSED,
                .ws = I2S_GPIO_UNUSED,
                .dout = I2S_GPIO_UNUSED,
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
        };
        return cfg;
    }
};

#endif  // BREAD_AUDIO_CODEC_H
