#include "Driver.h"

#include <Arduino.h>

#if FAN_AC_HARDWARE
#include <driver/gpio.h>
#include <esp_intr_alloc.h>
#include <esp_private/periph_ctrl.h>
#include <hal/gpio_ll.h>
#include <hal/timer_ll.h>
#include <soc/interrupts.h>

#include "Phase.h"

namespace {

constexpr int timerId = 0;
constexpr int timerGroup = 1;
constexpr auto alarmMask = TIMER_LL_EVENT_ALARM(timerId);
constexpr uint32_t xtalMHz = 40;
DRAM_ATTR portMUX_TYPE phaseLock = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR fan::Phase phase;
bool initialized = false;

inline __attribute__((always_inline)) uint64_t nowUs() {
    timer_ll_trigger_soft_capture(&TIMERG1, timerId);
    return timer_ll_get_counter_value(&TIMERG1, timerId);
}

inline __attribute__((always_inline)) void applyAlarm() {
    timer_ll_enable_alarm(&TIMERG1, timerId, false);
    timer_ll_clear_intr_status(&TIMERG1, alarmMask);
    const uint64_t due = phase.due();
    if (due) {
        timer_ll_set_alarm_value(&TIMERG1, timerId, due);
        if (due <= nowUs() + 5) phase.timingFault();
        else timer_ll_enable_alarm(&TIMERG1, timerId, true);
    }
    gpio_ll_set_level(&GPIO, fan::board::gate, phase.gate());
}

void IRAM_ATTR onZeroCross(void*) {
    portENTER_CRITICAL_ISR(&phaseLock);
    if (phase.edge(nowUs())) applyAlarm();
    portEXIT_CRITICAL_ISR(&phaseLock);
}

void IRAM_ATTR onTimer(void*) {
    portENTER_CRITICAL_ISR(&phaseLock);
    if (timer_ll_get_intr_status(&TIMERG1) & alarmMask) {
        timer_ll_clear_intr_status(&TIMERG1, alarmMask);
        if (phase.alarm(nowUs())) applyAlarm();
    }
    portEXIT_CRITICAL_ISR(&phaseLock);
}

} // namespace

namespace fan::driver {

bool safeBoot() {
    const auto gatePin = gpio_num_t(board::gate);
    gpio_set_level(gatePin, 0);
    gpio_config_t gateConfig{};
    gateConfig.pin_bit_mask = 1ULL << board::gate;
    gateConfig.mode = GPIO_MODE_OUTPUT;
    gateConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
    return gpio_config(&gateConfig) == ESP_OK;
}

bool begin() {
    if (!safeBoot()) return false;
    phase.configure(board::drive);
    if (getXtalFrequencyMhz() != xtalMHz) return false;
    gpio_config_t zeroConfig{};
    zeroConfig.pin_bit_mask = 1ULL << board::zeroCross;
    zeroConfig.mode = GPIO_MODE_INPUT;
    zeroConfig.intr_type = board::drive.fallingEdge ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    if (gpio_config(&zeroConfig) != ESP_OK) return false;
    if (gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1) != ESP_OK) return false;

    // Nie resetować grupy, bo może zawierać systemowy watchdog.
    periph_module_enable(PERIPH_TIMG1_MODULE);
    timer_ll_set_clock_source(timerGroup, timerId, GPTIMER_CLK_SRC_XTAL);
    timer_ll_enable_counter(&TIMERG1, timerId, false);
    timer_ll_set_clock_prescale(&TIMERG1, timerId, xtalMHz);
    timer_ll_set_count_direction(&TIMERG1, timerId, GPTIMER_COUNT_UP);
    timer_ll_enable_auto_reload(&TIMERG1, timerId, false);
    timer_ll_set_reload_value(&TIMERG1, timerId, 0);
    timer_ll_trigger_soft_reload(&TIMERG1, timerId);
    applyAlarm();

    if (esp_intr_alloc(ETS_TG1_T0_LEVEL_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL2,
                       onTimer, nullptr, nullptr) != ESP_OK) return false;
    if (gpio_isr_handler_add(gpio_num_t(board::zeroCross), onZeroCross, nullptr) != ESP_OK) {
        return false;
    }

    timer_ll_enable_intr(&TIMERG1, alarmMask, true);
    timer_ll_enable_counter(&TIMERG1, timerId, true);
    initialized = true;
    return true;
}

Drive status() {
    if (!initialized) return Drive::Fault;
    portENTER_CRITICAL(&phaseLock);
    const Drive result = phase.status(nowUs());
    portEXIT_CRITICAL(&phaseLock);
    return result;
}

void publish(uint8_t demand) {
    if (!initialized) return;
    portENTER_CRITICAL(&phaseLock);
    if (!phase.publish(nowUs(), demand)) applyAlarm();
    portEXIT_CRITICAL(&phaseLock);
}

} // namespace fan::driver

#else

namespace fan::driver {

namespace {
uint8_t simulatedDemand = 0;
}

bool safeBoot() { return true; }
bool begin() { return true; }
Drive status() { return Drive::Ready; }
void publish(uint8_t demand) { simulatedDemand = demand; }

} // namespace fan::driver
#endif
