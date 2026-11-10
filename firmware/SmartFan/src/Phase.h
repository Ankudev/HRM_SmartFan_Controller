#pragma once

#include "Core.h"

#ifdef ARDUINO
#include <esp_attr.h>
#define FAN_ISR IRAM_ATTR
#else
#define FAN_ISR
#endif

namespace fan {

class Phase {
public:
    static constexpr uint32_t minHalfUs = 9000;
    static constexpr uint32_t maxHalfUs = 11000;
    static constexpr uint32_t leaseUs = 30000;

    void configure(DriveProfile profile);
    Drive status(uint64_t now) const;
    bool publish(uint64_t now, uint8_t demand);
    bool FAN_ISR edge(uint64_t now);
    bool FAN_ISR alarm(uint64_t now);
    void FAN_ISR timingFault();
    bool gate() const { return gate_; }
    uint64_t due() const { return due_; }

private:
    void FAN_ISR cancel();
    bool FAN_ISR leaseValid(uint64_t now);

    DriveProfile profile_{};
    uint64_t lastEdge_ = 0;
    uint64_t lastPermit_ = 0;
    uint64_t due_ = 0;
    uint16_t delay_ = 0;
    uint8_t intervals_ = 0;
    bool seenEdge_ = false;
    bool gate_ = false;
    bool permitted_ = false;
    bool fault_ = false;
};

} // namespace fan
