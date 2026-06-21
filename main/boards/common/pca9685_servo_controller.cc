#include "pca9685_servo_controller.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <freertos/task.h>

#define TAG_SERVO "Pca9685Servo"

namespace {
constexpr int kServoDefaultMoveDurationMs = 2500;
constexpr uint16_t kServoPulseMinUs = 1000;
constexpr uint16_t kServoPulseMaxUs = 2000;
constexpr int kServoSpeedMin = 1;
constexpr int kServoSpeedMax = 100;

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool ParseSpeedLevel(const std::string& level, int& speed) {
    if (level.empty()) {
        return false;
    }
    if (level == "crawl" || level == "龟速" || level == "极慢") {
        speed = 8;
        return true;
    }
    if (level == "slow" || level == "慢" || level == "慢速") {
        speed = 18;
        return true;
    }
    if (level == "medium" || level == "中" || level == "中速" || level == "正常") {
        speed = 30;
        return true;
    }
    if (level == "fast" || level == "快" || level == "快速") {
        speed = 55;
        return true;
    }
    if (level == "max" || level == "全速" || level == "最快") {
        speed = 100;
        return true;
    }
    return false;
}

bool ExtractSpeedPrefix(std::string& action, int& speed) {
    struct SpeedPrefix {
        const char* prefix;
        int spd;
    };
    static const SpeedPrefix kItems[] = {{"慢速", 18}, {"缓慢", 15}, {"快速", 55}, {"全速", 100}, {"最快", 100},
                                         {"慢", 12},   {"快", 55},   {"slow", 18}, {"fast", 55},  {"max", 100}};
    for (const auto& item : kItems) {
        if (StartsWith(action, item.prefix)) {
            speed = item.spd;
            action = action.substr(strlen(item.prefix));
            while (!action.empty() && (action[0] == ' ' || action[0] == '_')) {
                action.erase(0, 1);
            }
            return true;
        }
    }
    return false;
}
}  // namespace

Pca9685ServoController::Pca9685ServoController(i2c_master_bus_handle_t i2c_bus, uint8_t pca9685_addr,
                                              float pwm_frequency_hz, const Pca9685ServoConfig& config)
    : i2c_bus_(i2c_bus),
      pca9685_addr_(pca9685_addr),
      pwm_frequency_hz_(pwm_frequency_hz),
      config_(config),
      default_move_speed_(config.default_speed) {
    mutex_ = xSemaphoreCreateRecursiveMutex();
    configASSERT(mutex_ != nullptr);

    // 每轮停转脉宽：优先用 per-wheel 配置，为 0 则沿用全局默认
    for (int i = 0; i < 4; ++i) {
        stop_pulse_us_[i] =
            config_.stop_pulse_per_wheel[i] != 0 ? config_.stop_pulse_per_wheel[i] : config_.stop_pulse_us;
    }
    wheel_gain_ = config_.wheel_gain;

    InitOePin();
    SetOutputsEnabled(false);

    esp_timer_create_args_t ramp_args = {
        .callback = &Pca9685ServoController::RampTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servo_ramp",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&ramp_args, &ramp_timer_));

    esp_timer_create_args_t dur_args = {
        .callback = &Pca9685ServoController::DurationTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servo_stop",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&dur_args, &duration_timer_));

    RegisterMcpTools();
    BootServoHold();

    ESP_LOGI(TAG_SERVO,
             "config invert=%d/%d/%d/%d sign=%d offset=%d~%d stop=%u/%u/%u/%u ramp=%d",
             config_.invert[0], config_.invert[1], config_.invert[2], config_.invert[3], config_.speed_sign,
             config_.speed_min_offset_us, config_.speed_max_offset_us, stop_pulse_us_[0], stop_pulse_us_[1],
             stop_pulse_us_[2], stop_pulse_us_[3], config_.ramp_enabled);
}

Pca9685ServoController::~Pca9685ServoController() {
    if (ramp_timer_ != nullptr) {
        esp_timer_stop(ramp_timer_);
        esp_timer_delete(ramp_timer_);
    }
    if (duration_timer_ != nullptr) {
        esp_timer_stop(duration_timer_);
        esp_timer_delete(duration_timer_);
    }
    {
        LockGuard lock(mutex_);
        if (initialized_) {
            StopLocked(true);
        }
        SetOutputsEnabled(false);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

// ── 硬件/初始化 ──────────────────────────────────────────────────────

void Pca9685ServoController::InitOePin() {
    if (config_.oe_pin < 0 || oe_pin_ready_) {
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << config_.oe_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    oe_pin_ready_ = true;
}

// PCA9685 OE 低电平有效：GPIO 高=关断舵机 PWM，GPIO 低=允许输出
void Pca9685ServoController::SetOutputsEnabled(bool enable) {
    if (config_.oe_pin < 0) {
        return;
    }
    InitOePin();
    gpio_set_level(static_cast<gpio_num_t>(config_.oe_pin), enable ? 0 : 1);
    ESP_LOGD(TAG_SERVO, "OE GPIO%d -> %s", config_.oe_pin, enable ? "ON" : "OFF");
}

bool Pca9685ServoController::EnsureHardware() {
    if (initialized_ && pca9685_ != nullptr) {
        return true;
    }
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG_SERVO, "I2C bus not ready");
        return false;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        pca9685_ = std::make_unique<Pca9685>(i2c_bus_, pca9685_addr_, pwm_frequency_hz_);
        // Init 把所有通道置全局停转脉宽(开机安全)；逐轮停转点在命令时由 FlushOutputs 应用
        if (pca9685_->Init(config_.stop_pulse_us, config_.stop_mode)) {
            initialized_ = true;
            ESP_LOGI(TAG_SERVO, "PCA9685 ready at 0x%02x (attempt %d)", pca9685_addr_, attempt + 1);
            return true;
        }
        ESP_LOGW(TAG_SERVO, "PCA9685 init retry %d/3", attempt + 1);
        pca9685_.reset();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGE(TAG_SERVO, "PCA9685 init failed at 0x%02x", pca9685_addr_);
    return false;
}

bool Pca9685ServoController::EnsureReadyForCommand() {
    if (!EnsureHardware()) {
        return false;
    }
    SetOutputsEnabled(true);
    return true;
}

void Pca9685ServoController::BootServoHold() {
    InitOePin();
    SetOutputsEnabled(false);
    if (config_.lazy_init) {
        ESP_LOGI(TAG_SERVO, "lazy init: OE off (GPIO%d), PWM on first voice command", config_.oe_pin);
        return;
    }
    LockGuard lock(mutex_);
    if (EnsureHardware()) {
        WriteStopPulsesLocked();   // 写各轮停转脉宽
        SetOutputsEnabled(false);  // 保持 OE 关，避免上电乱转
        ESP_LOGI(TAG_SERVO, "boot: outputs idle at stop pulses");
    } else {
        ESP_LOGE(TAG_SERVO, "boot: PCA9685 init failed, voice servo commands unavailable");
    }
}

// ── 速度映射 ────────────────────────────────────────────────────────

int Pca9685ServoController::ClampSpeedMag(int speed) const {
    if (speed < kServoSpeedMin) {
        return kServoSpeedMin;
    }
    if (speed > kServoSpeedMax) {
        return kServoSpeedMax;
    }
    return speed;
}

int Pca9685ServoController::ApplyInvert(int index, int speed) const {
    return config_.invert[index] ? -speed : speed;
}

int Pca9685ServoController::SpeedToOffsetUs(int speed_mag) const {
    if (speed_mag <= 0) {
        return 0;
    }
    // 在 [min_offset, max_offset] 区间内按速度插值：min>0 时可越过舵机启动死区，使低速也有效。
    const int lo = config_.speed_min_offset_us;
    int hi = config_.speed_max_offset_us;
    if (hi < lo) {
        hi = lo;
    }
    const int span = hi - lo;
    if (config_.quadratic_speed) {
        return lo + (speed_mag * speed_mag * span + 5000) / 10000;
    }
    return lo + (speed_mag * span + 50) / 100;
}

uint16_t Pca9685ServoController::SpeedToPulseUs(uint16_t center_us, int speed) const {
    speed = std::clamp(speed, -100, 100);
    const int mag = speed >= 0 ? speed : -speed;
    const int offset_us = SpeedToOffsetUs(mag);
    const int direction = speed > 0 ? 1 : (speed < 0 ? -1 : 0);
    int pulse = static_cast<int>(center_us);
    if (direction != 0 && offset_us > 0) {
        pulse += config_.speed_sign * direction * offset_us;
    }
    pulse = std::clamp(pulse, static_cast<int>(kServoPulseMinUs), static_cast<int>(kServoPulseMaxUs));
    return static_cast<uint16_t>(pulse);
}

// ── 运动控制(均需持有 mutex_) ──────────────────────────────────────

// 按 current_speeds_ 把四轮脉宽写进 PCA9685；相邻通道合并成一次 I2C 事务。
void Pca9685ServoController::FlushOutputsLocked() {
    if (!initialized_ || pca9685_ == nullptr) {
        return;
    }
    struct ChanPulse {
        uint8_t ch;
        uint16_t pulse;
    };
    ChanPulse items[4];
    for (int i = 0; i < 4; ++i) {
        items[i].ch = config_.channels[i];
        // 先按每轮增益缩放，再映射脉宽，用于补偿左右速度差
        int scaled = ApplyInvert(i, current_speeds_[i]) * wheel_gain_[i] / 100;
        items[i].pulse = SpeedToPulseUs(stop_pulse_us_[i], scaled);
    }
    std::sort(items, items + 4, [](const ChanPulse& a, const ChanPulse& b) { return a.ch < b.ch; });

    int i = 0;
    while (i < 4) {
        uint16_t run[4];
        run[0] = items[i].pulse;
        uint8_t start = items[i].ch;
        uint8_t count = 1;
        int j = i;
        while (j + 1 < 4 && items[j + 1].ch == static_cast<uint8_t>(items[j].ch + 1)) {
            run[count++] = items[j + 1].pulse;
            ++j;
        }
        pca9685_->SetPulseUsRun(start, run, count);
        i = j + 1;
    }
}

// 把每个轮子的停转脉宽写进对应通道，重复几次以确保可靠停转。
void Pca9685ServoController::WriteStopPulsesLocked() {
    if (!initialized_ || pca9685_ == nullptr) {
        return;
    }
    const int repeats = (config_.stop_mode == 0) ? 1 : 5;
    for (int r = 0; r < repeats; ++r) {
        for (int i = 0; i < 4; ++i) {
            pca9685_->SetPulseUs(config_.channels[i], stop_pulse_us_[i]);
        }
        if (r + 1 < repeats) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

bool Pca9685ServoController::IsMovingLocked() const {
    for (int i = 0; i < 4; ++i) {
        if (target_speeds_[i] != 0 || current_speeds_[i] != 0) {
            return true;
        }
    }
    return false;
}

void Pca9685ServoController::StartRampTimerLocked() {
    if (ramp_active_ || ramp_timer_ == nullptr) {
        return;
    }
    ramp_active_ = true;
    esp_timer_start_periodic(ramp_timer_, static_cast<int64_t>(config_.ramp_tick_ms) * 1000);
}

void Pca9685ServoController::ArmDurationTimerLocked(int duration_ms) {
    if (duration_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(duration_timer_);
    if (duration_ms > 0) {
        esp_timer_start_once(duration_timer_, static_cast<int64_t>(duration_ms) * 1000);
    }
}

void Pca9685ServoController::SetTargetsLocked(const std::array<int, 4>& targets, int duration_ms) {
    pending_disable_outputs_ = false;
    target_speeds_ = targets;
    ArmDurationTimerLocked(duration_ms);

    if (config_.ramp_enabled) {
        StartRampTimerLocked();  // 由斜坡周期逐步逼近目标
    } else {
        current_speeds_ = target_speeds_;
        FlushOutputsLocked();
    }
}

// 斜坡推进一步：每个轮子向 target 逼近至多 ramp_step。
void Pca9685ServoController::RampTickLocked() {
    bool changed = false;
    bool all_done = true;
    const int step = std::max(1, config_.ramp_step);
    for (int i = 0; i < 4; ++i) {
        if (current_speeds_[i] == target_speeds_[i]) {
            continue;
        }
        int diff = target_speeds_[i] - current_speeds_[i];
        int delta = std::clamp(diff, -step, step);
        current_speeds_[i] += delta;
        changed = true;
        if (current_speeds_[i] != target_speeds_[i]) {
            all_done = false;
        }
    }
    if (changed) {
        FlushOutputsLocked();
    }
    if (all_done) {
        if (ramp_timer_ != nullptr) {
            esp_timer_stop(ramp_timer_);
        }
        ramp_active_ = false;
        if (pending_disable_outputs_) {
            // 已平滑减速到 0：写各轮停转脉宽再关 OE
            WriteStopPulsesLocked();
            SetOutputsEnabled(false);
            pending_disable_outputs_ = false;
            ESP_LOGI(TAG_SERVO, "ramped stop done");
        }
    }
}

// immediate=true：立即截停(安全/紧急)；false：平滑减速到 0 后关 OE。
void Pca9685ServoController::StopLocked(bool immediate) {
    target_speeds_ = {0, 0, 0, 0};
    if (duration_timer_ != nullptr) {
        esp_timer_stop(duration_timer_);
    }

    if (!immediate && config_.ramp_enabled && initialized_) {
        // 仍在动则平滑停；已经停了就直接关 OE
        bool moving = false;
        for (int i = 0; i < 4; ++i) {
            if (current_speeds_[i] != 0) {
                moving = true;
            }
        }
        if (moving) {
            pending_disable_outputs_ = true;
            StartRampTimerLocked();
            return;
        }
    }

    // 立即停止路径
    if (ramp_timer_ != nullptr) {
        esp_timer_stop(ramp_timer_);
    }
    ramp_active_ = false;
    pending_disable_outputs_ = false;
    current_speeds_ = {0, 0, 0, 0};

    if (initialized_ && pca9685_ != nullptr) {
        SetOutputsEnabled(false);  // 先关输出，仅在芯片内更新寄存器
        WriteStopPulsesLocked();
        ESP_LOGI(TAG_SERVO, "stop all (mode=%u sign=%d)", config_.stop_mode, config_.speed_sign);
    }
    SetOutputsEnabled(false);
}

void Pca9685ServoController::AdjustSpeedsLocked(int delta) {
    for (int i = 0; i < 4; ++i) {
        if (target_speeds_[i] == 0) {
            continue;
        }
        int sign = target_speeds_[i] > 0 ? 1 : -1;
        int mag = ClampSpeedMag(std::abs(target_speeds_[i]) + delta);
        target_speeds_[i] = sign * mag;
    }
    if (config_.ramp_enabled) {
        StartRampTimerLocked();
    } else {
        current_speeds_ = target_speeds_;
        FlushOutputsLocked();
    }
}

// ── 命令解析 ────────────────────────────────────────────────────────

int Pca9685ServoController::ResolveMoveSpeed(int param_speed, const std::string& speed_level,
                                             std::string& action) const {
    int speed = param_speed;
    if (ParseSpeedLevel(speed_level, speed)) {
        return ClampSpeedMag(speed);
    }
    if (ExtractSpeedPrefix(action, speed)) {
        return ClampSpeedMag(speed);
    }
    if (param_speed <= 0) {
        return ClampSpeedMag(default_move_speed_);
    }
    return ClampSpeedMag(param_speed);
}

// 弧线转向：base 为前进(+)/后退(-)基准速度，turn∈[-100,100]，正=右转、负=左转。
// 内侧轮按比例减速，turn 足够大时内侧轮反向 => 原地打转。
void Pca9685ServoController::ComputeArc(int base, int turn, int turn_gain, std::array<int, 4>& wheels) {
    int fl = base, fr = base, rl = base, rr = base;
    if (turn != 0 && base != 0) {
        int factor = std::abs(base) * std::abs(turn) / 100;  // 内侧减速幅度
        factor = factor * turn_gain / 100;
        int reduced = base >= 0 ? base - factor : base + factor;  // 朝/过 0 减
        if (turn > 0) {  // 右转 -> 右侧轮慢
            fr = rr = reduced;
        } else {  // 左转 -> 左侧轮慢
            fl = rl = reduced;
        }
    }
    wheels = {fl, fr, rl, rr};
}

bool Pca9685ServoController::ParseMoveAction(const std::string& action, int speed, int turn,
                                            std::array<int, 4>& wheels) const {
    if (action == "stop" || action == "停止" || action == "停下" || action == "停") {
        wheels = {0, 0, 0, 0};
        return true;
    }
    if (action == "forward" || action == "前进" || action == "向前" || action == "前" ||
        action == "go_forward" || action == "go") {
        ComputeArc(speed, turn, config_.turn_gain, wheels);
        return true;
    }
    if (action == "backward" || action == "后退" || action == "向后" || action == "后" ||
        action == "go_backward") {
        ComputeArc(-speed, turn, config_.turn_gain, wheels);
        return true;
    }
    // 弧线动作关键词(默认带 60% 转向，可被显式 turn 覆盖)
    int arc_turn = turn != 0 ? turn : 60;
    if (action == "forward_left" || action == "前进左转" || action == "左前" || action == "前左") {
        ComputeArc(speed, -arc_turn, config_.turn_gain, wheels);
        return true;
    }
    if (action == "forward_right" || action == "前进右转" || action == "右前" || action == "前右") {
        ComputeArc(speed, arc_turn, config_.turn_gain, wheels);
        return true;
    }
    if (action == "backward_left" || action == "后退左转" || action == "左后") {
        ComputeArc(-speed, -arc_turn, config_.turn_gain, wheels);
        return true;
    }
    if (action == "backward_right" || action == "后退右转" || action == "右后") {
        ComputeArc(-speed, arc_turn, config_.turn_gain, wheels);
        return true;
    }
    // 原地自旋
    if (action == "turn_left" || action == "左转" || action == "向左" || action == "左") {
        wheels = {-speed, speed, -speed, speed};
        return true;
    }
    if (action == "turn_right" || action == "右转" || action == "向右" || action == "右") {
        wheels = {speed, -speed, speed, -speed};
        return true;
    }
    return false;
}

bool Pca9685ServoController::ParseServoIndex(const std::string& name, int& index) const {
    if (name == "fl" || name == "0") {
        index = 0;
        return true;
    }
    if (name == "fr" || name == "1") {
        index = 1;
        return true;
    }
    if (name == "rl" || name == "2") {
        index = 2;
        return true;
    }
    if (name == "rr" || name == "3") {
        index = 3;
        return true;
    }
    return false;
}

std::string Pca9685ServoController::CommandFailed() const {
    return std::string("舵机未就绪(I2C/PCA9685)，请查 config.h 中 SDA/SCL/OE 引脚与接线、VCC；") +
           (initialized_ ? "已初始化" : "初始化失败");
}

// ── 定时器回调 ──────────────────────────────────────────────────────

void Pca9685ServoController::RampTimerCallback(void* arg) {
    auto* self = static_cast<Pca9685ServoController*>(arg);
    LockGuard lock(self->mutex_);
    self->RampTickLocked();
}

void Pca9685ServoController::DurationTimerCallback(void* arg) {
    auto* self = static_cast<Pca9685ServoController*>(arg);
    LockGuard lock(self->mutex_);
    self->StopLocked(false);  // 到时平滑停车
}

// ── MCP 工具注册 ────────────────────────────────────────────────────

void Pca9685ServoController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.servo.set_speed",
        "设置单个360度连续旋转舵机的转速。servo: fl/fr/rl/rr；speed: -100~100，0为停止。",
        PropertyList({Property("servo", kPropertyTypeString),
                      Property("speed", kPropertyTypeInteger, 0, -100, 100)}),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            if (!EnsureReadyForCommand()) {
                return CommandFailed();
            }
            int index = 0;
            if (!ParseServoIndex(properties["servo"].value<std::string>(), index)) {
                return "无效的舵机名称，请使用 fl/fr/rl/rr";
            }
            std::array<int, 4> targets = target_speeds_;
            targets[index] = std::clamp(properties["speed"].value<int>(), -100, 100);
            if (targets == std::array<int, 4>{0, 0, 0, 0}) {
                StopLocked(false);
            } else {
                SetTargetsLocked(targets, 0);
            }
            return true;
        });

    mcp_server.AddTool(
        "self.servo.set_all",
        "同时设置四个舵机转速。fl/fr/rl/rr 取值 -100~100，0 为停止。",
        PropertyList({Property("fl", kPropertyTypeInteger, 0, -100, 100),
                      Property("fr", kPropertyTypeInteger, 0, -100, 100),
                      Property("rl", kPropertyTypeInteger, 0, -100, 100),
                      Property("rr", kPropertyTypeInteger, 0, -100, 100)}),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            if (!EnsureReadyForCommand()) {
                return CommandFailed();
            }
            std::array<int, 4> targets = {
                std::clamp(properties["fl"].value<int>(), -100, 100),
                std::clamp(properties["fr"].value<int>(), -100, 100),
                std::clamp(properties["rl"].value<int>(), -100, 100),
                std::clamp(properties["rr"].value<int>(), -100, 100)};
            if (targets == std::array<int, 4>{0, 0, 0, 0}) {
                StopLocked(false);
            } else {
                SetTargetsLocked(targets, 0);
            }
            return true;
        });

    mcp_server.AddTool(
        "self.servo.move",
        "【重要】四轮连续旋转舵机小车。用户说前进/后退/转向/停下时必须调用本工具。"
        "速度：speed 为 1~100(越大越快)；0=使用默认速度。"
        "或 speed_level: crawl/slow/medium/fast/max(龟速/慢/中/快/全速，中英文均可)。"
        "action 也可写「慢速前进」「快速后退」「前进左转」「前进右转」等。"
        "turn: -100~100，边走边拐弯(负=左拐、正=右拐，0=直行)；与 forward/backward 配合走弧线。"
        "起步与停车自动平滑加减速。"
        "用户说「快点」「再快一点」用 self.servo.change_speed；设默认速度用 self.servo.set_default_speed。",
        PropertyList({Property("action", kPropertyTypeString, std::string("forward")),
                      Property("speed", kPropertyTypeInteger, 0, 0, 100),
                      Property("speed_level", kPropertyTypeString, std::string("")),
                      Property("turn", kPropertyTypeInteger, 0, -100, 100),
                      Property("duration_ms", kPropertyTypeInteger, kServoDefaultMoveDurationMs, 0, 60000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string action = properties["action"].value<std::string>();
            int param_speed = properties["speed"].value<int>();
            const std::string& speed_level = properties["speed_level"].value<std::string>();
            int turn = std::clamp(properties["turn"].value<int>(), -100, 100);
            int duration_ms = properties["duration_ms"].value<int>();

            LockGuard lock(mutex_);
            if (action == "stop" || action == "停止" || action == "停下" || action == "停") {
                if (!EnsureHardware()) {
                    SetOutputsEnabled(false);
                    return true;
                }
                StopLocked(false);
                return true;
            }

            if (!EnsureReadyForCommand()) {
                return CommandFailed();
            }

            int speed = ResolveMoveSpeed(param_speed, speed_level, action);
            std::array<int, 4> wheels{};
            if (!ParseMoveAction(action, speed, turn, wheels)) {
                return std::string("无效 action: ") + action;
            }

            ESP_LOGI(TAG_SERVO, "move action=%s speed=%d turn=%d duration_ms=%d", action.c_str(), speed,
                     turn, duration_ms);
            SetTargetsLocked(wheels, duration_ms);
            char buf[64];
            snprintf(buf, sizeof(buf), "speed=%d turn=%d (平滑加减速)", speed, turn);
            return std::string(buf);
        });

    mcp_server.AddTool(
        "self.servo.set_default_speed",
        "设置小车默认行驶速度(1~100)，之后 self.servo.move 在未指定 speed/speed_level 时使用。",
        PropertyList({Property("speed", kPropertyTypeInteger, 30, 1, 100)}),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            default_move_speed_ = ClampSpeedMag(properties["speed"].value<int>());
            char buf[48];
            snprintf(buf, sizeof(buf), "default_speed=%d", default_move_speed_);
            return std::string(buf);
        });

    mcp_server.AddTool(
        "self.servo.change_speed",
        "行驶中加快或减慢：delta 为正加快、为负减慢(建议±10~25)。若当前静止则改默认速度。",
        PropertyList({Property("delta", kPropertyTypeInteger, 15, -50, 50)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int delta = properties["delta"].value<int>();
            if (delta == 0) {
                return std::string("delta=0，无变化");
            }
            LockGuard lock(mutex_);
            if (!EnsureReadyForCommand()) {
                return CommandFailed();
            }
            if (IsMovingLocked()) {
                AdjustSpeedsLocked(delta);
                char buf[96];
                snprintf(buf, sizeof(buf), "{\"fl\":%d,\"fr\":%d,\"rl\":%d,\"rr\":%d}", target_speeds_[0],
                         target_speeds_[1], target_speeds_[2], target_speeds_[3]);
                return std::string(buf);
            }
            default_move_speed_ = ClampSpeedMag(default_move_speed_ + delta);
            char buf[48];
            snprintf(buf, sizeof(buf), "default_speed=%d", default_move_speed_);
            return std::string(buf);
        });

    mcp_server.AddTool("self.servo.stop", "立即停止全部舵机并关闭PWM输出(紧急截停，不走减速)", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           LockGuard lock(mutex_);
                           if (!EnsureHardware()) {
                               SetOutputsEnabled(false);
                               return true;
                           }
                           StopLocked(true);
                           return true;
                       });

    mcp_server.AddTool(
        "self.servo.set_stop_pulse",
        "校准360°舵机停转脉宽(us)。servo: all/fl/fr/rl/rr(默认all)。设置后持续输出该脉宽(OE保持开)，"
        "便于观察该轮是否停转：还在转就微调脉宽，直到几乎不动即为该轮停转点。常见1450~1600。",
        PropertyList({Property("pulse_us", kPropertyTypeInteger, 1500, 1300, 1700),
                      Property("servo", kPropertyTypeString, std::string("all"))}),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            if (!EnsureReadyForCommand()) {  // 校准需要 OE 开、持续输出
                return CommandFailed();
            }
            uint16_t pulse = static_cast<uint16_t>(properties["pulse_us"].value<int>());
            std::string servo = properties["servo"].value<std::string>();
            int index = 0;
            if (servo == "all" || servo.empty()) {
                stop_pulse_us_ = {pulse, pulse, pulse, pulse};
            } else if (ParseServoIndex(servo, index)) {
                stop_pulse_us_[index] = pulse;
            } else {
                return "无效的舵机名称，请用 all/fl/fr/rl/rr";
            }
            // 停掉斜坡与时长定时器，按停转脉宽持续输出(current=0)，OE 保持开以便观察
            if (ramp_timer_ != nullptr) esp_timer_stop(ramp_timer_);
            if (duration_timer_ != nullptr) esp_timer_stop(duration_timer_);
            ramp_active_ = false;
            pending_disable_outputs_ = false;
            target_speeds_ = {0, 0, 0, 0};
            current_speeds_ = {0, 0, 0, 0};
            WriteStopPulsesLocked();
            ESP_LOGI(TAG_SERVO, "set_stop_pulse servo=%s pulse=%u -> stop=%u/%u/%u/%u", servo.c_str(), pulse,
                     stop_pulse_us_[0], stop_pulse_us_[1], stop_pulse_us_[2], stop_pulse_us_[3]);
            char buf[96];
            snprintf(buf, sizeof(buf), "stop[FL/FR/RL/RR]=%u/%u/%u/%u", stop_pulse_us_[0], stop_pulse_us_[1],
                     stop_pulse_us_[2], stop_pulse_us_[3]);
            return std::string(buf);
        });

    mcp_server.AddTool(
        "self.servo.set_gain",
        "校正左右轮速差走直线。servo: left/right/all/fl/fr/rl/rr(默认right)；gain 百分比10~150(越小越慢)。"
        "右轮偏快、车往左偏 → 把 right 调小(如 60~80)；偏右 → 把 right 调大或 left 调小。即时生效。",
        PropertyList({Property("servo", kPropertyTypeString, std::string("right")),
                      Property("gain", kPropertyTypeInteger, 70, 10, 150)}),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            int gain = std::clamp(properties["gain"].value<int>(), 10, 150);
            std::string servo = properties["servo"].value<std::string>();
            int index = 0;
            if (servo == "right" || servo == "右") {
                wheel_gain_[1] = wheel_gain_[3] = gain;
            } else if (servo == "left" || servo == "左") {
                wheel_gain_[0] = wheel_gain_[2] = gain;
            } else if (servo == "all" || servo.empty()) {
                wheel_gain_ = {gain, gain, gain, gain};
            } else if (ParseServoIndex(servo, index)) {
                wheel_gain_[index] = gain;
            } else {
                return "无效名称，请用 left/right/all/fl/fr/rl/rr";
            }
            FlushOutputsLocked();  // 行驶中即时应用
            char buf[80];
            snprintf(buf, sizeof(buf), "gain[FL/FR/RL/RR]=%d/%d/%d/%d", wheel_gain_[0], wheel_gain_[1],
                     wheel_gain_[2], wheel_gain_[3]);
            return std::string(buf);
        });

    mcp_server.AddTool(
        "self.servo.get_state",
        "获取四轮目标转速(±100)、默认速度、是否在动/是否就绪。speed 绝对值越大转得越快。",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            LockGuard lock(mutex_);
            char buf[352];
            snprintf(buf, sizeof(buf),
                     "{\"fl\":%d,\"fr\":%d,\"rl\":%d,\"rr\":%d,\"default_speed\":%d,\"moving\":%s,"
                     "\"ramping\":%s,\"ready\":%s,\"stop_us\":[%u,%u,%u,%u],\"gain\":[%d,%d,%d,%d]}",
                     target_speeds_[0], target_speeds_[1], target_speeds_[2], target_speeds_[3],
                     default_move_speed_, IsMovingLocked() ? "true" : "false",
                     ramp_active_ ? "true" : "false", initialized_ ? "true" : "false", stop_pulse_us_[0],
                     stop_pulse_us_[1], stop_pulse_us_[2], stop_pulse_us_[3], wheel_gain_[0], wheel_gain_[1],
                     wheel_gain_[2], wheel_gain_[3]);
            return std::string(buf);
        });
}
