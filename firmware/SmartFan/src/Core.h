#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fan {

constexpr uint32_t hrTimeoutMs = 5000;
constexpr uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
constexpr size_t zoneCount = 5;

struct Config {
    std::array<uint16_t, zoneCount> threshold{105, 125, 145, 165, 200};
    std::array<uint16_t, zoneCount> demand{30, 45, 60, 75, 95};
    uint16_t hysteresis = 3;
    uint16_t filterMs = 2000;
    uint16_t risePerSecond = 15;
    uint16_t fallPerSecond = 8;
    uint16_t manual = 45;
};

constexpr size_t configBytes = 2 * (2 * zoneCount + 5);
using ConfigBytes = std::array<uint8_t, configBytes>;

bool valid(const Config& config);
ConfigBytes encode(const Config& config);
bool decode(const uint8_t* data, size_t size, Config& config);
uint16_t read16(const uint8_t* data);
uint32_t read32(const uint8_t* data);
void write16(uint8_t* data, uint16_t value);
void write32(uint8_t* data, uint32_t value);
uint32_t crc32(const uint8_t* data, size_t size);

struct DriveProfile {
    bool approved = false;
    uint16_t minimum = 0;
    uint16_t maximum = 0;
    uint16_t startDemand = 0;
    uint16_t startMs = 0;
    uint16_t earliestUs = 0;
    uint16_t latestUs = 0;
    uint16_t gateUs = 0;
    bool fallingEdge = false;
};

bool valid(const DriveProfile& profile);

enum class Contact : uint8_t { Unknown, Present, Absent };
struct HeartRate {
    uint16_t bpm = 0;
    Contact contact = Contact::Unknown;
};
bool parseHeartRate(const uint8_t* data, size_t size, HeartRate& result);
bool usable(HeartRate hr);

enum class Mode : uint8_t { Off, Auto, Manual, Pair, Config };
enum class Reason : uint8_t {
    Off, Running, Waiting, Stale, NoContact, NoLink, Profile, InvalidConfig,
    Sync, Fault, Pair, Config
};
enum class Drive : uint8_t { Ready, Waiting, Fault };
struct Snapshot {
    Mode mode = Mode::Off;
    Reason reason = Reason::Off;
    uint16_t bpm = 0;
    uint8_t demand = 0;
    bool linked = false;
    bool fresh = false;
    uint16_t manual = 0;
};

class Control {
public:
    void configure(const Config& config, bool configOk, const DriveProfile& profile);
    void select(Mode mode);
    void changeManual(int step);
    void link(uint32_t session);
    void unlink(uint32_t session);
    void loseInput();
    void sample(uint32_t session, uint32_t received, HeartRate hr);
    Snapshot tick(uint32_t now, Drive drive);

private:
    void clearOutput();
    uint8_t ramp(uint16_t target, uint32_t now, uint32_t dt);
    uint16_t autoDemand();

    Config config_{};
    DriveProfile profile_{};
    Mode mode_ = Mode::Off;
    uint16_t manual_ = 0;
    uint32_t session_ = 0;
    uint32_t measuredAt_ = 0;
    uint32_t tickAt_ = 0;
    uint32_t startAt_ = 0;
    HeartRate hr_{};
    float filtered_ = 0;
    float output_ = 0;
    size_t zone_ = 0;
    unsigned good_ = 0;
    bool configOk_ = false;
    bool started_ = false;
    bool syncSeen_ = false;
};

class Backoff {
public:
    bool ready(uint32_t now) const { return elapsed(now, since_) >= delay_; }
    void fail(uint32_t now);
    void reset() { delay_ = 0; }

private:
    uint32_t since_ = 0;
    uint32_t delay_ = 0;
};

} // namespace fan
