#include "ConfigBle.h"

#include <algorithm>
#include <cstring>
#include <esp_random.h>

namespace {

constexpr char serviceId[] = "578c0001-a943-42e2-90d7-e9eb82a2a89b";
constexpr char rxId[] = "578c0002-a943-42e2-90d7-e9eb82a2a89b";
constexpr char txId[] = "578c0003-a943-42e2-90d7-e9eb82a2a89b";
constexpr char name[] = "SmartFan v2";

bool trusted(const NimBLEConnInfo& peer) {
    return peer.isEncrypted() && peer.isAuthenticated() && peer.isBonded();
}

} // namespace

namespace fan::app {

bool ConfigBle::begin() {
    input_ = xQueueCreate(12, sizeof(Fragment));
    if (!input_) return false;
    server_ = NimBLEDevice::createServer();
    if (!server_) return false;
    server_->setCallbacks(this, false);
    server_->advertiseOnDisconnect(false);
    auto* service = server_->createService(serviceId);
    if (!service) return false;
    auto* rx = service->createCharacteristic(
        rxId, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN,
        sizeof(Fragment::bytes));
    tx_ = service->createCharacteristic(txId, NIMBLE_PROPERTY::NOTIFY, 20);
    if (!rx || !tx_) return false;
    rx->setCallbacks(this);

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    advertising_ = NimBLEDevice::getAdvertising();
    if (!advertising_) return false;
    NimBLEAdvertisementData adv, scan;
    adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    ready_ = adv.addServiceUUID(serviceId) && scan.setName(name) &&
        advertising_->setAdvertisementData(adv) && advertising_->setScanResponseData(scan);
    advertising_->enableScanResponse(true);
    return ready_;
}

void ConfigBle::onConnect(NimBLEServer* server, NimBLEConnInfo& info) {
    if (!accepts_.load() || peer_.load()) {
        server->disconnect(info);
        return;
    }
    generation_ = (generation_ % 65535) + 1;
    badPeer_.store(0);
    peer_.store((generation_ << 16) | info.getConnHandle());
}

void ConfigBle::onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) {
    const auto token = peer_.load();
    if (token && uint16_t(token) == info.getConnHandle()) {
        peer_.store(0);
        pin_.store(1000000);
    }
}

uint32_t ConfigBle::onPassKeyDisplay() {
    const uint32_t value = esp_random() % 1000000;
    pin_.store(value);
    return value;
}

void ConfigBle::onAuthenticationComplete(NimBLEConnInfo& info) {
    pin_.store(1000000);
    if (!accepts_.load() || !trusted(info)) server_->disconnect(info);
}

void ConfigBle::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) {
    const auto token = peer_.load();
    if (!accepts_.load() || !token || uint16_t(token) != info.getConnHandle() || !trusted(info)) return;
    const auto value = characteristic->getValue();
    Fragment fragment{};
    if (value.size() == 0 || value.size() > sizeof(fragment.bytes)) {
        badPeer_.store(token);
        return;
    }
    fragment.peer = token;
    fragment.received = millis();
    fragment.size = uint8_t(value.size());
    std::memcpy(fragment.bytes, value.data(), value.size());
    if (xQueueSend(input_, &fragment, 0) != pdTRUE) badPeer_.store(token);
}

void ConfigBle::stop() {
    if (!ready_) return;
    accepts_.store(false);
    if (active_) {
        advertising_->stop();
        active_ = false;
    }
    const auto token = peer_.load();
    if (token) server_->disconnect(uint16_t(token));
    session_.close();
    sessionPeer_ = 0;
    closing_ = false;
    stream_.reset();
    pin_.store(1000000);
}

bool ConfigBle::current(uint32_t token, const Request& request) const {
    return token && peer_.load() == token && badPeer_.load() != token &&
        request.mode == Mode::Config && request.epoch == epoch_ && request.forget == forgotten_ &&
        elapsed(millis(), request.since) < wire::windowMs;
}

void ConfigBle::read(const Fragment& fragment, const Request& initial) {
    if (fragment.peer != sessionPeer_) return;
    for (size_t i = 0; i < fragment.size; ++i) {
        Request request = initial;
        xQueuePeek(radioRequest, &request, 0);
        if (!current(fragment.peer, request)) return;
        if (!stream_.push(fragment.bytes[i], fragment.received)) continue;
        bool committed = false;
        const auto handle = uint16_t(fragment.peer);
        const auto info = server_->getPeerInfoByHandle(handle);
        const bool close = stream_.frame().op() == uint8_t(wire::Op::Close);
        const auto reply = session_.handle(stream_.frame(), millis(), trusted(info), committed);
        stream_.reset();
        if (committed) {
            ConfigView config{};
            config.valid = storage_.load(config.config);
            xQueueOverwrite(configInput, &config);
        }
        for (size_t offset = 0; offset < reply.size; offset += 20) {
            xQueuePeek(radioRequest, &request, 0);
            if (!current(fragment.peer, request)) break;
            const size_t length = std::min(reply.size - offset, size_t(20));
            if (!tx_->notify(reply.bytes.data() + offset, length, handle)) break;
        }
        if (close && reply.size && reply.payload()[0] == uint8_t(wire::Result::Ok)) {
            closing_ = true;
            closingAt_ = millis();
            return;
        }
    }
}

void ConfigBle::tick(const Request& request, RadioView& view) {
    const auto now = millis();
    if (!ready_) {
        if (request.mode == Mode::Config) view.state = RadioState::RadioError;
        return;
    }

    if (forgotten_ != request.forget) {
        forgotten_ = request.forget;
        revoke_ = true;
    }
    if (revoke_) {
        stop();
        if (!closed()) return;
        view.state = NimBLEDevice::deleteAllBonds() ? RadioState::BondsRemoved : RadioState::BondError;
        revoke_ = false;
        return;
    }

    if (request.mode != Mode::Config || elapsed(now, request.since) >= wire::windowMs ||
        (active_ && epoch_ != request.epoch)) {
        stop();
        return;
    }

    if (!active_) {
        if (!closed()) return;
        active_ = true;
        epoch_ = request.epoch;
        lastAdvertise_ = now - 1000;
        accepts_.store(true);
    }

    const auto token = peer_.load();
    if (token != sessionPeer_) {
        sessionPeer_ = token;
        closing_ = false;
        session_.open(request.since);
        stream_.reset();
    }
    if (token && badPeer_.load() == token) {
        server_->disconnect(uint16_t(token));
        session_.close();
    }

    view.state = RadioState::Config;
    view.pin = pin_.load();
    if (!token && elapsed(now, lastAdvertise_) >= 1000 && !advertising_->isAdvertising()) {
        lastAdvertise_ = now;
        if (!advertising_->start()) view.state = RadioState::RadioError;
    }

    if (closing_) {
        if (elapsed(now, closingAt_) >= 750 && token) server_->disconnect(uint16_t(token));
        return;
    }
    Fragment fragment{};
    for (int i = 0; !closing_ && i < 12 && xQueueReceive(input_, &fragment, 0) == pdTRUE; ++i) {
        read(fragment, request);
    }
    stream_.expire(millis());
}

} // namespace fan::app
