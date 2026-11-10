#include <NimBLEDevice.h>

#include <cstdio>
#include <cstring>

#include "ConfigBle.h"

namespace fan::app {
namespace {

constexpr size_t maxCandidates = 12;
constexpr uint32_t sampleWaitMs = 8000;
const NimBLEUUID hrService(uint16_t(0x180d)), hrMeasurement(uint16_t(0x2a37));

struct Candidate {
    NimBLEAddress address;
    char label[25]{};
};

class Radio {
public:
    void begin();
    void tick(const Request& request);

private:
    bool attach(const NimBLEAddress& address);
    bool detach();
    void pollSamples(const Request& request);
    void scan();
    void publishCandidates();

    Storage storage_;
    ConfigBle config_{storage_};
    NimBLEClient* client_ = nullptr;
    QueueHandle_t inbox_ = nullptr;
    Backoff backoff_;
    RadioView view_{};
    NimBLEAddress saved_;
    Candidate candidates_[maxCandidates]{};
    uint32_t session_ = 0;
    uint32_t nextSeen_ = 0;
    uint32_t confirmSeen_ = 0;
    uint32_t heardAt_ = 0;
    uint32_t checkingSince_ = 0;
    uint32_t disconnectAt_ = 0;
    bool disconnectPending_ = false;
    uint32_t forgotten_ = 0;
    bool selecting_ = false;
    bool paired_ = false;
    bool savedKnown_ = false;
    bool ready_ = false;
};

void Radio::begin() {
    storage_.begin();
    ConfigView active{};
    active.valid = storage_.load(active.config);
    xQueueOverwrite(configInput, &active);

    std::array<uint8_t, 7> stored{};
    if (storage_.readSensor(stored)) {
        saved_ = NimBLEAddress(stored.data() + 1, stored[0]);
        savedKnown_ = true;
    }

    inbox_ = xQueueCreate(16, sizeof(Sample));
    if (!inbox_ || !NimBLEDevice::init("SmartFan v2")) return;
    ready_ = config_.begin();
    client_ = NimBLEDevice::createClient();
    if (client_) client_->setConnectTimeout(3000);
    ready_ = ready_ && client_;
    radioReady.store(ready_);
}

bool Radio::detach() {
    hrSession.store(0);
    if (!client_->isConnected()) {
        disconnectPending_ = false;
        return true;
    }
    const auto now = millis();
    if (!disconnectPending_ || elapsed(now, disconnectAt_) >= 200) {
        disconnectPending_ = true;
        disconnectAt_ = now;
        client_->disconnect();
    }
    return false;
}

bool Radio::attach(const NimBLEAddress& address) {
    ++view_.attempts;
    disconnectPending_ = false;
    if (!client_->connect(address, true, false, false)) return false;
    auto* service = client_->getService(hrService);
    auto* measurement = service ? service->getCharacteristic(hrMeasurement) : nullptr;
    if (!measurement || !measurement->canNotify() ||
        !measurement->getDescriptor(NimBLEUUID(uint16_t(0x2902)))) return false;
    if (++session_ == 0) ++session_;
    const uint32_t epoch = session_;
    const bool ok = measurement->subscribe(true, [queue = inbox_, epoch](
            NimBLERemoteCharacteristic*, uint8_t* bytes, size_t size, bool) {
        Sample sample{epoch, millis(), {}};
        if (parseHeartRate(bytes, size, sample.hr) && xQueueSend(queue, &sample, 0) != pdTRUE) {
            hrOverflow.store(true);
        }
    });
    if (ok) heardAt_ = millis();
    return ok;
}

void Radio::pollSamples(const Request& request) {
    Sample sample{};
    for (int i = 0; i < 16 && xQueueReceive(inbox_, &sample, 0) == pdTRUE; ++i) {
        if (!client_->isConnected() || sample.session != session_) continue;
        if (usable(sample.hr) || sample.hr.contact == Contact::Absent) heardAt_ = sample.received;
        if (selecting_) {
            if (paired_ || !usable(sample.hr) || elapsed(millis(), sample.received) >= hrTimeoutMs) continue;
            Request current{};
            xQueuePeek(radioRequest, &current, 0);
            if (current.mode != Mode::Pair || current.epoch != request.epoch ||
                elapsed(millis(), current.since) >= pairWindowMs) return;
            const NimBLEAddress address = candidates_[view_.index].address;
            const auto* base = address.getBase();
            std::array<uint8_t, 7> encoded{};
            encoded[0] = base->type;
            std::memcpy(encoded.data() + 1, base->val, 6);
            paired_ = storage_.saveSensor(encoded);
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
        if (hrSession.load() == sample.session && xQueueSend(hrInput, &sample, 0) != pdTRUE) {
            hrOverflow.store(true);
        }
    }
}

void Radio::publishCandidates() {
    const auto& candidate = candidates_[view_.index];
    std::memcpy(view_.candidate, candidate.label, sizeof(view_.candidate));
    const std::string address = candidate.address.toString();
    std::snprintf(view_.address, sizeof(view_.address), "%s", address.c_str());
    view_.state = RadioState::Choose;
}

void Radio::scan() {
    view_.state = RadioState::Scanning;
    xQueueOverwrite(radioView, &view_);
    auto* scan = NimBLEDevice::getScan();
    if (!scan) {
        view_.state = RadioState::RadioError;
        return;
    }
    scan->setMaxResults(32);
    scan->setActiveScan(true);
    const auto found = scan->getResults(2500, false);
    view_.count = 0;
    view_.index = 0;
    for (const bool advertisedHr : {true, false}) {
        for (int i = 0; i < found.getCount() && view_.count < maxCandidates; ++i) {
            const auto* device = found.getDevice(i);
            if (!device || !device->isConnectable() || device->isAdvertisingService(hrService) != advertisedHr) {
                continue;
            }
            auto& candidate = candidates_[view_.count++];
            candidate.address = device->getAddress();
            std::snprintf(candidate.label, sizeof(candidate.label), "%s",
                          device->haveName() ? device->getName().c_str() : "(bez nazwy)");
            for (char& c : candidate.label) {
                if (c && (c < 32 || c > 126)) c = '?';
            }
        }
    }
    scan->clearResults();
    if (view_.count) publishCandidates();
    else view_.state = RadioState::NoCandidates;
}

void Radio::tick(const Request& request) {
    if (!ready_) {
        view_.state = RadioState::RadioError;
        xQueueOverwrite(radioView, &view_);
        return;
    }

    const bool changed = request.epoch != view_.epoch;
    if (changed) {
        view_.epoch = request.epoch;
        view_.pin = 1000000;
    }
    const auto now = millis();
    if (request.mode == Mode::Config || !config_.closed() || request.forget != forgotten_) {
        configBusy.store(true);
        if (detach()) {
            config_.tick(request, view_);
            forgotten_ = request.forget;
        }
    } else {
        configBusy.store(false);
        if (request.mode == Mode::Pair) {
            if (changed) {
                nextSeen_ = request.next;
                confirmSeen_ = request.confirm;
                view_.state = RadioState::Idle;
                selecting_ = paired_ = false;
            }
            if (view_.state == RadioState::Idle && detach()) scan();
            if (view_.state == RadioState::Choose || view_.state == RadioState::PairFailed) {
                if (nextSeen_ != request.next) {
                    nextSeen_ = request.next;
                    view_.index = (view_.index + 1) % view_.count;
                    publishCandidates();
                }
                if (confirmSeen_ != request.confirm && detach()) {
                    confirmSeen_ = request.confirm;
                    view_.state = RadioState::Checking;
                    xQueueOverwrite(radioView, &view_);
                    selecting_ = attach(candidates_[view_.index].address);
                    checkingSince_ = millis();
                    if (!selecting_) {
                        detach();
                        view_.state = RadioState::PairFailed;
                    }
                }
            }
            if (selecting_) {
                pollSamples(request);
                if (selecting_ && !paired_ &&
                    (!client_->isConnected() || elapsed(millis(), checkingSince_) >= sampleWaitMs)) {
                    selecting_ = false;
                    detach();
                    view_.state = RadioState::PairFailed;
                }
            }
        } else if (selecting_) {
            selecting_ = paired_ = false;
            detach();
        } else {
            if (hrSession.load() && (!client_->isConnected() || elapsed(now, heardAt_) >= sampleWaitMs)) {
                detach();
                backoff_.fail(now);
            }
            if (hrSession.load()) {
                view_.state = RadioState::Subscribed;
                pollSamples(request);
            } else if (client_->isConnected()) {
                detach();
            } else if (savedKnown_ && backoff_.ready(now)) {
                view_.state = RadioState::Connecting;
                xQueueOverwrite(radioView, &view_);
                if (attach(saved_)) {
                    hrSession.store(session_);
                    backoff_.reset();
                } else {
                    detach();
                    backoff_.fail(millis());
                }
            } else if (view_.state != RadioState::BondsRemoved && view_.state != RadioState::BondError) {
                view_.state = RadioState::Idle;
            }
        }
    }
    xQueueOverwrite(radioView, &view_);
}

} // namespace

void radioTask(void*) {
    static Radio radio;
    radio.begin();
    Request request{};
    for (;;) {
        xQueuePeek(radioRequest, &request, 0);
        radio.tick(request);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace fan::app
