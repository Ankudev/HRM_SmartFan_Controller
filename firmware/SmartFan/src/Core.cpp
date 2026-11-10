#include "Core.h"

#include <algorithm>
#include <cmath>

namespace fan {

bool valid(const Config& c) {
    if (c.hysteresis > 10 || c.filterMs > 10000 || c.risePerSecond < 1 ||
        c.risePerSecond > 100 || c.fallPerSecond < 1 || c.fallPerSecond > 100 ||
        c.manual > 100) return false;
    for (size_t i = 0; i < zoneCount; ++i) {
        if (c.threshold[i] < 30 || c.threshold[i] > 240 || c.demand[i] > 100) return false;
        if (i && (c.threshold[i] <= c.threshold[i - 1] + c.hysteresis ||
                  c.demand[i] < c.demand[i - 1])) return false;
    }
    return true;
}

uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) { return uint32_t(read16(p)) | (uint32_t(read16(p + 2)) << 16); }

void write16(uint8_t* p, uint16_t n) {
    p[0] = uint8_t(n);
    p[1] = uint8_t(n >> 8);
}

void write32(uint8_t* p, uint32_t n) {
    write16(p, uint16_t(n));
    write16(p + 2, uint16_t(n >> 16));
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
        }
    }
    return ~crc;
}

ConfigBytes encode(const Config& c) {
    ConfigBytes bytes{};
    for (size_t i = 0; i < zoneCount; ++i) {
        write16(&bytes[2 * i], c.threshold[i]);
        write16(&bytes[2 * (i + zoneCount)], c.demand[i]);
    }
    write16(&bytes[20], c.hysteresis);
    write16(&bytes[22], c.filterMs);
    write16(&bytes[24], c.risePerSecond);
    write16(&bytes[26], c.fallPerSecond);
    write16(&bytes[28], c.manual);
    return bytes;
}

bool decode(const uint8_t* p, size_t size, Config& config) {
    if (size != configBytes) return false;
    Config candidate;
    for (size_t i = 0; i < zoneCount; ++i) {
        candidate.threshold[i] = read16(p + 2 * i);
        candidate.demand[i] = read16(p + 2 * (i + zoneCount));
    }
    candidate.hysteresis = read16(p + 20);
    candidate.filterMs = read16(p + 22);
    candidate.risePerSecond = read16(p + 24);
    candidate.fallPerSecond = read16(p + 26);
    candidate.manual = read16(p + 28);
    if (!valid(candidate)) return false;
    config = candidate;
    return true;
}

bool valid(const DriveProfile& p) {
    const bool startupOk = (p.startMs == 0 && p.startDemand == 0) ||
        (p.startMs > 0 && p.startMs <= 2000 &&
         p.startDemand >= p.minimum && p.startDemand <= p.maximum);
    return p.approved && p.minimum >= 1 && p.minimum <= p.maximum && p.maximum <= 100 &&
        startupOk && p.earliestUs >= 250 && p.earliestUs < p.latestUs &&
        p.gateUs >= 40 && p.gateUs <= 500 && unsigned(p.latestUs) + p.gateUs <= 8600;
}

bool parseHeartRate(const uint8_t* data, size_t size, HeartRate& result) {
    if (!data || size < 2) return false;
    const uint8_t flags = data[0];
    const bool wide = flags & 1;
    const size_t hrEnd = wide ? 3 : 2;
    const size_t fixedEnd = hrEnd + ((flags & 8) ? 2 : 0);
    if (size < fixedEnd) return false;
    const size_t remaining = size - fixedEnd;
    if ((flags & 16) && remaining < 2) return false;
    result.bpm = wide ? read16(data + 1) : data[1];
    result.contact = !(flags & 4) ? Contact::Unknown : ((flags & 2) ? Contact::Present : Contact::Absent);
    return true;
}

bool usable(HeartRate hr) { return hr.bpm >= 25 && hr.bpm <= 250 && hr.contact != Contact::Absent; }

void Control::configure(const Config& c, bool ok, const DriveProfile& p) {
    configOk_ = ok && valid(c);
    config_ = configOk_ ? c : Config{};
    profile_ = p;
    manual_ = config_.manual;
    select(Mode::Off);
}

void Control::clearOutput() {
    output_ = 0;
    started_ = false;
}

void Control::select(Mode mode) {
    mode_ = mode;
    syncSeen_ = false;
    clearOutput();
}

void Control::changeManual(int step) {
    manual_ = uint16_t(std::clamp(int(manual_) + step, 0, 100));
}

void Control::link(uint32_t session) {
    session_ = session;
    loseInput();
}

void Control::unlink(uint32_t session) {
    if (session == session_) link(0);
}

void Control::loseInput() {
    good_ = 0;
    hr_ = {};
    zone_ = 0;
    if (mode_ == Mode::Auto) clearOutput();
}

void Control::sample(uint32_t session, uint32_t now, HeartRate hr) {
    if (!session_ || session != session_) return;
    if (hr.contact == Contact::Absent) {
        loseInput();
        hr_ = hr;
        return;
    }
    if (!usable(hr)) return;
    const uint32_t dt = elapsed(now, measuredAt_);
    if (dt >= hrTimeoutMs) loseInput();
    if (!good_ || !config_.filterMs) {
        filtered_ = hr.bpm;
    } else {
        const float alpha = -std::expm1(-float(dt) / config_.filterMs);
        filtered_ += (float(hr.bpm) - filtered_) * alpha;
    }
    if (good_ < 2) ++good_;
    measuredAt_ = now;
    hr_ = hr;
}

uint16_t Control::autoDemand() {
    while (zone_ < zoneCount && filtered_ >= config_.threshold[zone_]) ++zone_;
    while (zone_ && filtered_ < config_.threshold[zone_ - 1] - config_.hysteresis) --zone_;
    return zone_ ? config_.demand[zone_ - 1] : 0;
}

uint8_t Control::ramp(uint16_t target, uint32_t now, uint32_t dt) {
    if (!target) {
        clearOutput();
        return 0;
    }
    target = std::clamp(target, profile_.minimum, profile_.maximum);
    if (!started_) {
        started_ = true;
        startAt_ = now;
        output_ = profile_.minimum;
        dt = 0;
    }
    if (elapsed(now, startAt_) < profile_.startMs) {
        output_ = std::max(target, profile_.startDemand);
    } else {
        const float seconds = float(dt) / 1000.0f;
        output_ += std::clamp(float(target) - output_, -seconds * config_.fallPerSecond,
                              seconds * config_.risePerSecond);
    }
    return uint8_t(std::clamp(std::lround(output_), long(profile_.minimum), long(profile_.maximum)));
}

Snapshot Control::tick(uint32_t now, Drive drive) {
    const uint32_t dt = std::min(elapsed(now, tickAt_), uint32_t(100));
    tickAt_ = now;
    const bool stale = good_ && elapsed(now, measuredAt_) >= hrTimeoutMs;
    if (stale) loseInput();
    Snapshot s{mode_, Reason::Off, hr_.bpm, 0, session_ != 0, good_ != 0, manual_};
    if (mode_ == Mode::Config) s.reason = Reason::Config;
    else if (mode_ == Mode::Pair) s.reason = Reason::Pair;
    else if (drive == Drive::Fault || (syncSeen_ && drive != Drive::Ready)) {
        select(Mode::Off);
        s.mode = mode_;
        s.reason = Reason::Fault;
    } else if (!configOk_) s.reason = Reason::InvalidConfig;
    else if (!valid(profile_)) s.reason = Reason::Profile;
    else if (mode_ == Mode::Off) return s;
    else if (drive == Drive::Waiting) s.reason = Reason::Sync;
    else {
        syncSeen_ = true;
        if (mode_ == Mode::Manual) {
            s.demand = ramp(manual_, now, dt);
            s.reason = Reason::Running;
            return s;
        }
        if (!session_) s.reason = Reason::NoLink;
        else if (hr_.contact == Contact::Absent) s.reason = Reason::NoContact;
        else if (good_ < 2) s.reason = good_ ? Reason::Waiting : Reason::Stale;
        else {
            s.demand = ramp(autoDemand(), now, dt);
            s.reason = Reason::Running;
            return s;
        }
    }
    clearOutput();
    return s;
}

void Backoff::fail(uint32_t now) {
    since_ = now;
    delay_ = delay_ ? std::min(delay_ * 2, uint32_t(30000)) : 1000;
}

} // namespace fan
