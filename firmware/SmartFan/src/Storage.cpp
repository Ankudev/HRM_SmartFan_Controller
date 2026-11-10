#include "Storage.h"

namespace fan {

bool Storage::begin() {
    ready_ = nvs_.begin("smartfan-v2", false);
    if (ready_ && !nvs_.isKey("config")) {
        Config def{};
        def.smoothCurve = true;
        def.cooldownSec = 60;
        save(def);
    }
    return ready_;
}

bool Storage::readRecord(wire::Record& record) {
    return ready_ && nvs_.getBytesLength("config") == record.size() &&
        nvs_.getBytes("config", record.data(), record.size()) == record.size();
}

bool Storage::load(Config& config) {
    wire::Record record{};
    if (!readRecord(record) || !wire::decodeRecord(record, config)) return false;
    config.smoothCurve = nvs_.getBool("smooth", true);
    config.cooldownSec = nvs_.getUShort("cooldown", 60);
    if (config.cooldownSec > 600) config.cooldownSec = 60;
    return true;
}

bool Storage::save(const Config& config) {
    if (!ready_ || !valid(config)) return false;
    const auto candidate = wire::encodeRecord(config);
    wire::Record readback{};
    if (!readRecord(readback) || candidate != readback) {
        if (nvs_.putBytes("config", candidate.data(), candidate.size()) != candidate.size()) return false;
        if (!readRecord(readback) || candidate != readback) return false;
    }
    nvs_.putBool("smooth", config.smoothCurve);
    nvs_.putUShort("cooldown", config.cooldownSec);
    return true;
}

bool Storage::readSensor(std::array<uint8_t, 7>& sensor) {
    return ready_ && nvs_.getBytesLength("sensor") == sensor.size() &&
        nvs_.getBytes("sensor", sensor.data(), sensor.size()) == sensor.size() && sensor[0] <= 1;
}

bool Storage::saveSensor(const std::array<uint8_t, 7>& sensor) {
    if (!ready_ || sensor[0] > 1) return false;
    std::array<uint8_t, 7> check{};
    if (readSensor(check) && check == sensor) return true;
    return nvs_.putBytes("sensor", sensor.data(), sensor.size()) == sensor.size() &&
        readSensor(check) && check == sensor;
}

} // namespace fan
