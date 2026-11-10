#include "Storage.h"

namespace fan {

bool Storage::begin() {
    ready_ = nvs_.begin("smartfan-v2", false);
    return ready_;
}

bool Storage::readRecord(wire::Record& record) {
    return ready_ && nvs_.getBytesLength("config") == record.size() &&
        nvs_.getBytes("config", record.data(), record.size()) == record.size();
}

bool Storage::load(Config& config) {
    wire::Record record{};
    return readRecord(record) && wire::decodeRecord(record, config);
}

bool Storage::save(const Config& config) {
    if (!ready_ || !valid(config)) return false;
    const auto candidate = wire::encodeRecord(config);
    wire::Record readback{};
    if (readRecord(readback) && candidate == readback) return true;
    if (nvs_.putBytes("config", candidate.data(), candidate.size()) != candidate.size()) return false;
    return readRecord(readback) && candidate == readback;
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
