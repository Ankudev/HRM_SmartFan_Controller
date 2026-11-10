#pragma once

#include <Arduino.h>
#include <atomic>

#include "Core.h"

namespace fan::app {

enum class RadioState : uint8_t {
    Boot, Idle, Connecting, Subscribed, Scanning, Choose, Checking,
    NoCandidates, PairFailed, PairSaved, Config, RadioError, BondsRemoved, BondError
};

struct Request {
    Mode mode = Mode::Off;
    uint32_t epoch = 0;
    uint32_t since = 0;
    uint32_t next = 0;
    uint32_t confirm = 0;
    uint32_t forget = 0;
};

struct RadioView {
    RadioState state = RadioState::Boot;
    uint32_t epoch = 0;
    uint32_t pin = 1000000;
    uint32_t attempts = 0;
    uint8_t index = 0;
    uint8_t count = 0;
    char candidate[25]{};
    char address[18]{};
};

struct Sample {
    uint32_t session;
    uint32_t received;
    HeartRate hr;
};

struct ConfigView {
    Config config;
    bool valid;
};

inline QueueHandle_t radioRequest;
inline QueueHandle_t radioView;
inline QueueHandle_t controlView;
inline QueueHandle_t hrInput;
inline QueueHandle_t configInput;
inline std::atomic<uint32_t> hrSession{0};
inline std::atomic<bool> hrOverflow{false};
inline std::atomic<bool> configBusy{false};
inline std::atomic<bool> uiReady{false};
inline std::atomic<bool> radioReady{false};
inline constexpr uint32_t pairWindowMs = 60000;

void radioTask(void*);
void controlTask(void*);
bool displayBegin();
void displayUpdate();

} // namespace fan::app
