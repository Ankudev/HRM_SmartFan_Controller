#pragma once

#include <Preferences.h>

#include "Protocol.h"

namespace fan {

class Storage final : public wire::Store {
public:
    bool begin();
    bool load(Config& config) override;
    bool save(const Config& config) override;
    bool readSensor(std::array<uint8_t, 7>& sensor);
    bool saveSensor(const std::array<uint8_t, 7>& sensor);

private:
    bool readRecord(wire::Record& record);
    Preferences nvs_;
    bool ready_ = false;
};

} // namespace fan
