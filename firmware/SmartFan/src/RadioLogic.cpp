#include "RadioLogic.h"

#include <cstdio>
#include <cstring>

namespace fan::app {

void formatAddress(const SensorAddress& address, char (&text)[18]) {
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", unsigned(address[6]),
                  unsigned(address[5]), unsigned(address[4]), unsigned(address[3]),
                  unsigned(address[2]), unsigned(address[1]));
}

void RadioLogic::begin(bool ready, const SensorAddress* saved) {
    if (saved) {
        saved_ = *saved;
        savedKnown_ = true;
    }
    ready_ = ready;
}

bool RadioLogic::detach() {
    port_.link(0);
    if (!port_.connected()) {
        disconnectPending_ = false;
        return true;
    }
    const auto now = port_.now();
    if (!disconnectPending_ || elapsed(now, disconnectAt_) >= disconnectRetryMs) {
        disconnectPending_ = true;
        disconnectAt_ = now;
        port_.disconnect();
    }
    return false;
}

bool RadioLogic::attach(const SensorAddress& address) {
    ++view_.attempts;
    disconnectPending_ = false;
    if (++session_ == 0) ++session_;
    if (!port_.attach(address, session_)) return false;
    heardAt_ = port_.now();
    return true;
}

void RadioLogic::pollSamples(const Request& request) {
    Sample sample{};
    for (int i = 0; i < 16 && port_.receive(sample); ++i) {
        if (!port_.connected() || sample.session != session_) continue;
        if (usable(sample.hr) || sample.hr.contact == Contact::Absent) heardAt_ = sample.received;
        if (selecting_) {
            if (paired_ || !usable(sample.hr) || elapsed(port_.now(), sample.received) >= hrTimeoutMs) continue;
            Request current{};
            port_.currentRequest(current);
            if (current.mode != Mode::Pair || current.epoch != request.epoch ||
                elapsed(port_.now(), current.since) >= pairWindowMs) return;
            const SensorAddress address = candidates_[view_.index].address;
            paired_ = port_.saveSensor(address);
            view_.state = paired_ ? RadioState::PairSaved : RadioState::PairFailed;
            if (paired_) {
                saved_ = address;
                savedKnown_ = true;
                backoff_.reset();
            } else {
                selecting_ = false;
                detach();
            }
            return;
        }
        if (port_.linked() == sample.session && !port_.forward(sample)) port_.overflow();
    }
}

void RadioLogic::publishCandidate() {
    const auto& candidate = candidates_[view_.index];
    std::memcpy(view_.candidate, candidate.label, sizeof(view_.candidate));
    formatAddress(candidate.address, view_.address);
    view_.state = RadioState::Choose;
}

void RadioLogic::scan() {
    view_.state = RadioState::Scanning;
    port_.publish(view_);
    const int found = port_.scan(found_, maxScan);
    if (found < 0) {
        view_.state = RadioState::RadioError;
        return;
    }
    const int total = found < int(maxScan) ? found : int(maxScan);
    view_.count = 0;
    view_.index = 0;
    for (const bool advertisedHr : {true, false}) {
        for (int i = 0; i < total && view_.count < maxCandidates; ++i) {
            const auto& device = found_[i];
            if (!device.connectable || device.heartRate != advertisedHr) continue;
            auto& candidate = candidates_[view_.count++];
            candidate.address = device.address;
            std::snprintf(candidate.label, sizeof(candidate.label), "%.*s", int(sizeof(device.name) - 1),
                          device.name[0] ? device.name : "(bez nazwy)");
            for (char& c : candidate.label) {
                const auto byte = static_cast<unsigned char>(c);
                if (byte && (byte < 32 || byte > 126)) c = '?';
            }
            auto& item = view_.items[view_.count - 1];
            std::memcpy(item.label, candidate.label, sizeof(item.label));
            formatAddress(candidate.address, item.address);
            item.heartRate = advertisedHr;
        }
    }
    if (view_.count) publishCandidate();
    else view_.state = RadioState::NoCandidates;
}

void RadioLogic::pairTick(const Request& request, bool changed) {
    if (changed) {
        nextSeen_ = request.next;
        confirmSeen_ = request.confirm;
        view_.state = RadioState::Idle;
        selecting_ = paired_ = false;
    }
    if (view_.state == RadioState::Idle && detach()) scan();
    if (view_.state == RadioState::Choose || view_.state == RadioState::PairFailed) {
        if (nextSeen_ != request.next && view_.count) {
            const uint32_t step = request.next - nextSeen_;
            nextSeen_ = request.next;
            view_.index = uint8_t((view_.index + step) % view_.count);
            publishCandidate();
        }
        if (confirmSeen_ != request.confirm && detach()) {
            confirmSeen_ = request.confirm;
            view_.state = RadioState::Checking;
            port_.publish(view_);
            const SensorAddress address = candidates_[view_.index].address;
            if (attach(address) && port_.saveSensor(address)) {
                paired_ = true;
                saved_ = address;
                savedKnown_ = true;
                port_.link(session_);
                backoff_.reset();
                view_.state = RadioState::PairSaved;
            } else {
                selecting_ = false;
                detach();
                view_.state = RadioState::PairFailed;
            }
        }
    }
    if (selecting_) {
        pollSamples(request);
        if (selecting_ && !paired_ &&
            (!port_.connected() || elapsed(port_.now(), checkingSince_) >= sampleWaitMs)) {
            selecting_ = false;
            detach();
            view_.state = RadioState::PairFailed;
        }
    }
}

void RadioLogic::linkTick(const Request& request, uint32_t now) {
    if (port_.linked() && (!port_.connected() || elapsed(now, heardAt_) >= sampleWaitMs)) {
        detach();
        backoff_.fail(now);
    }
    if (port_.linked()) {
        view_.state = RadioState::Subscribed;
        pollSamples(request);
    } else if (port_.connected()) {
        detach();
    } else if (savedKnown_ && backoff_.ready(now)) {
        view_.state = RadioState::Connecting;
        port_.publish(view_);
        if (attach(saved_)) {
            port_.link(session_);
            backoff_.reset();
        } else {
            detach();
            backoff_.fail(port_.now());
        }
    } else if (view_.state != RadioState::BondsRemoved && view_.state != RadioState::BondError) {
        view_.state = RadioState::Idle;
    }
}

void RadioLogic::tick(const Request& request) {
    if (!ready_) {
        view_.state = RadioState::RadioError;
        port_.publish(view_);
        return;
    }

    const bool changed = request.epoch != view_.epoch;
    if (changed) {
        view_.epoch = request.epoch;
        view_.pin = 1000000;
    }
    const auto now = port_.now();
    if (request.mode == Mode::Config || !port_.configClosed() || request.forget != forgotten_) {
        port_.configBusy(true);
        if (detach()) {
            port_.configTick(request, view_);
            forgotten_ = request.forget;
        }
    } else {
        port_.configBusy(false);
        if (request.mode == Mode::Pair) {
            pairTick(request, changed);
        } else if (selecting_) {
            selecting_ = paired_ = false;
            detach();
        } else {
            linkTick(request, now);
        }
    }
    port_.publish(view_);
}

} // namespace fan::app

