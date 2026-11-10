#pragma once

#include <Arduino.h>
#include <atomic>

#include "Core.h"
#include "RadioLogic.h"

namespace fan::app {

struct ConfigView {
    Config config;
    bool valid;
};

enum class UiAction : uint8_t { None, SetMode, SetManual, PairSelect };

struct UiCommand {
    UiAction action = UiAction::None;
    Mode mode = Mode::Off;
    uint8_t value = 0;
};

inline QueueHandle_t radioRequest;
inline QueueHandle_t radioView;
inline QueueHandle_t controlView;
inline QueueHandle_t hrInput;
inline QueueHandle_t configInput;
inline QueueHandle_t activeConfigView;
inline QueueHandle_t configSaveRequest;
inline QueueHandle_t uiCommand;
inline std::atomic<uint32_t> hrSession{0};
inline std::atomic<bool> hrOverflow{false};
inline std::atomic<bool> configBusy{false};
inline std::atomic<bool> uiReady{false};
inline std::atomic<bool> radioReady{false};

void radioTask(void*);
void controlTask(void*);
bool displayBegin();
void displayUpdate();
void webBegin();
void webUpdate();

} // namespace fan::app
