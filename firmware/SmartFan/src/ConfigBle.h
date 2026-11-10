#pragma once

#include <NimBLEDevice.h>

#include "App.h"
#include "Storage.h"

namespace fan::app {

class ConfigBle final : private NimBLEServerCallbacks, private NimBLECharacteristicCallbacks {
public:
    explicit ConfigBle(Storage& storage) : storage_(storage), session_(storage) {}
    bool begin();
    void tick(const Request& request, RadioView& view);
    bool closed() const { return !active_ && !peer_.load(); }

private:
    struct Fragment {
        uint32_t peer;
        uint32_t received;
        uint8_t size;
        uint8_t bytes[2 * wire::maxFrame];
    };

    void onConnect(NimBLEServer*, NimBLEConnInfo& peer) override;
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& peer, int reason) override;
    uint32_t onPassKeyDisplay() override;
    void onAuthenticationComplete(NimBLEConnInfo&) override;
    void onWrite(NimBLECharacteristic*, NimBLEConnInfo& peer) override;
    void stop();
    void read(const Fragment& fragment, const Request& request);
    bool current(uint32_t peer, const Request& request) const;

    Storage& storage_;
    wire::Session session_;
    wire::Stream stream_;
    QueueHandle_t input_ = nullptr;
    NimBLEServer* server_ = nullptr;
    NimBLEAdvertising* advertising_ = nullptr;
    NimBLECharacteristic* tx_ = nullptr;
    std::atomic<uint32_t> peer_{0};
    std::atomic<uint32_t> pin_{1000000};
    std::atomic<bool> accepts_{false};
    std::atomic<uint32_t> badPeer_{0};
    uint32_t generation_ = 0;
    uint32_t sessionPeer_ = 0;
    uint32_t epoch_ = 0;
    uint32_t forgotten_ = 0;
    uint32_t lastAdvertise_ = 0;
    uint32_t closingAt_ = 0;
    bool closing_ = false;
    bool active_ = false;
    bool revoke_ = false;
    bool ready_ = false;
};

} // namespace fan::app
