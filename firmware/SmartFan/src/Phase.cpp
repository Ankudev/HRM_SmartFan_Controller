#include "Phase.h"

namespace fan {

void Phase::configure(DriveProfile profile) {
    *this = {};
    profile_ = profile;
}

Drive Phase::status(uint64_t now) const {
    if (fault_ || (permitted_ && now - lastPermit_ >= leaseUs)) return Drive::Fault;
    return intervals_ >= 3 && now - lastEdge_ <= maxHalfUs ? Drive::Ready : Drive::Waiting;
}

void Phase::cancel() {
    due_ = 0;
    gate_ = false;
}

bool Phase::leaseValid(uint64_t now) {
    if (permitted_ && now - lastPermit_ >= leaseUs) {
        permitted_ = false;
        fault_ = true;
    }
    return permitted_ && !fault_;
}

bool Phase::publish(uint64_t now, uint8_t demand) {
    leaseValid(now);
    if (permitted_ && now - lastEdge_ > maxHalfUs) fault_ = true;
    if (!demand || demand < profile_.minimum || demand > profile_.maximum ||
        !valid(profile_) || status(now) != Drive::Ready) {
        permitted_ = false;
        cancel();
        return false;
    }
    const uint32_t range = profile_.latestUs - profile_.earliestUs;
    const uint32_t span = profile_.maximum - profile_.minimum;
    delay_ = span ? profile_.latestUs - (range * (demand - profile_.minimum) / span)
                  : profile_.latestUs;
    lastPermit_ = now;
    permitted_ = true;
    return true;
}

bool Phase::edge(uint64_t now) {
    const uint64_t half = now - lastEdge_;
    if (seenEdge_ && half < minHalfUs) return false;
    if (!seenEdge_ || half > maxHalfUs) {
        intervals_ = 0;
        if (permitted_) fault_ = true;
        permitted_ = false;
    } else if (intervals_ < 3) {
        ++intervals_;
    }
    seenEdge_ = true;
    lastEdge_ = now;
    cancel();
    if (intervals_ >= 3 && leaseValid(now)) due_ = now + delay_;
    return true;
}

void Phase::timingFault() {
    fault_ = true;
    permitted_ = false;
    cancel();
}

bool Phase::alarm(uint64_t now) {
    if (!due_ || now < due_) return false;
    if (gate_) {
        cancel();
    } else if (!leaseValid(now)) {
        cancel();
    } else if (now - due_ > 100 || now - lastEdge_ + profile_.gateUs > 8700) {
        timingFault();
    } else {
        gate_ = true;
        due_ = now + profile_.gateUs;
    }
    return true;
}

} // namespace fan
