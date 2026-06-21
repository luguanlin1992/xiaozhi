// PCA9685 四轮连续旋转舵机控制器。
// MCP 工具 self.servo.*，支持平滑加减速(斜坡)、弧线转向、批量 I2C、线程安全。
//
// 设计说明：本控制器编译进 boards/common（只编一次），看不到各板的 config.h 宏，
// 因此所有可调项通过 Pca9685ServoConfig 在板级代码里(已包含 config.h)填好后传入，
// 逻辑与宏解耦，便于复用与测试。
#ifndef PCA9685_SERVO_CONTROLLER_H
#define PCA9685_SERVO_CONTROLLER_H

#include "mcp_server.h"
#include "pca9685.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 舵机/小车可调参数。板级用 config.h 的 SERVO_* 宏填充后传入构造函数。
struct Pca9685ServoConfig {
    std::array<uint8_t, 4> channels{0, 1, 2, 3};  // FL/FR/RL/RR 对应 PCA9685 通道
    std::array<bool, 4> invert{false, false, false, false};

    int oe_pin = -1;             // PCA9685 OE 接的 GPIO；-1=不接(OE 接 GND)
    uint16_t stop_pulse_us = 1500;  // 全局默认停转脉宽
    // 每轮独立停转脉宽(FL/FR/RL/RR)；0=沿用全局 stop_pulse_us。
    // 连续旋转舵机每个停转点常有差异，逐轮校准后填这里，"前进/停止"才会左右一致。
    std::array<uint16_t, 4> stop_pulse_per_wheel{0, 0, 0, 0};
    uint8_t stop_mode = 1;       // 0=写 1 次停止脉宽，1=连写 5 次(更可靠)
    int speed_sign = -1;           // 前进方向符号
    int speed_min_offset_us = 0;   // speed=1 时的偏移(死区补偿)：>0 让低速也能越过舵机启动死区
    int speed_max_offset_us = 18;  // speed=100 时相对停止脉宽的最大偏移
    bool quadratic_speed = false;  // 二次速度曲线(低速更细腻)
    bool lazy_init = true;       // 上电仅关 OE，首次语音命令再初始化 PCA9685
    int default_speed = 30;      // 未指定速度时的默认行驶速度

    // 平滑加减速(斜坡)
    bool ramp_enabled = true;
    int ramp_tick_ms = 20;       // 斜坡更新周期
    int ramp_step = 8;           // 每个周期速度变化上限(1~100)；越小越柔

    int turn_gain = 100;         // 弧线转向强度(%)：100=turn 满量时内侧轮可反转原地打转

    // 每轮速度增益(%)，用于补偿左右舵机速度不一致(走直线)。100=不变，右轮偏快就把右轮调小。
    std::array<int, 4> wheel_gain{100, 100, 100, 100};
};

class Pca9685ServoController {
public:
    Pca9685ServoController(i2c_master_bus_handle_t i2c_bus, uint8_t pca9685_addr,
                           float pwm_frequency_hz, const Pca9685ServoConfig& config);
    ~Pca9685ServoController();

    bool IsInitialized() const { return initialized_; }

private:
    // ── 硬件/初始化 ──────────────────────────────────────────────
    void InitOePin();
    void SetOutputsEnabled(bool enable);
    bool EnsureHardware();
    bool EnsureReadyForCommand();
    void BootServoHold();

    // ── 速度映射 ────────────────────────────────────────────────
    int ClampSpeedMag(int speed) const;
    int ApplyInvert(int index, int speed) const;
    int SpeedToOffsetUs(int speed_mag) const;
    uint16_t SpeedToPulseUs(uint16_t center_us, int speed) const;  // 以该轮停转点为中心

    // ── 运动控制(均需持有 mutex_) ──────────────────────────────
    void FlushOutputsLocked();                 // 按 current_speeds_ 写 PCA9685(批量)
    void WriteStopPulsesLocked();              // 把每轮停转脉宽写进 PCA9685(可靠重复)
    void RampTickLocked();                      // 斜坡推进一步
    void SetTargetsLocked(const std::array<int, 4>& targets, int duration_ms);
    void StopLocked(bool immediate);            // immediate=true 立即截停；false 平滑减速到停
    void StartRampTimerLocked();
    void ArmDurationTimerLocked(int duration_ms);
    bool IsMovingLocked() const;
    void AdjustSpeedsLocked(int delta);

    // ── 命令解析 ────────────────────────────────────────────────
    int ResolveMoveSpeed(int param_speed, const std::string& speed_level, std::string& action) const;
    bool ParseMoveAction(const std::string& action, int speed, int turn,
                         std::array<int, 4>& wheels) const;
    static void ComputeArc(int base, int turn, int turn_gain, std::array<int, 4>& wheels);
    bool ParseServoIndex(const std::string& name, int& index) const;
    std::string CommandFailed() const;

    // ── 定时器回调 ──────────────────────────────────────────────
    static void RampTimerCallback(void* arg);
    static void DurationTimerCallback(void* arg);

    void RegisterMcpTools();

    // RAII 递归锁
    struct LockGuard {
        SemaphoreHandle_t m;
        explicit LockGuard(SemaphoreHandle_t mtx) : m(mtx) { xSemaphoreTakeRecursive(m, portMAX_DELAY); }
        ~LockGuard() { xSemaphoreGiveRecursive(m); }
    };

    i2c_master_bus_handle_t i2c_bus_;
    uint8_t pca9685_addr_;
    float pwm_frequency_hz_;
    Pca9685ServoConfig config_;
    std::unique_ptr<Pca9685> pca9685_;

    SemaphoreHandle_t mutex_ = nullptr;
    esp_timer_handle_t ramp_timer_ = nullptr;
    esp_timer_handle_t duration_timer_ = nullptr;

    bool initialized_ = false;
    bool oe_pin_ready_ = false;
    bool ramp_active_ = false;
    bool pending_disable_outputs_ = false;
    std::array<uint16_t, 4> stop_pulse_us_{1500, 1500, 1500, 1500};  // 每轮停转脉宽

    std::array<int, 4> target_speeds_{};   // 命令目标速度(-100~100)
    std::array<int, 4> current_speeds_{};  // 当前已输出的速度(斜坡逼近 target)
    std::array<int, 4> wheel_gain_{100, 100, 100, 100};  // 每轮速度增益(%)
    int default_move_speed_ = 30;
};

#endif  // PCA9685_SERVO_CONTROLLER_H
