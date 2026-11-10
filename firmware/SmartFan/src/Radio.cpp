#include <NimBLEDevice.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "ConfigBle.h"

namespace fan::app {
namespace {

const NimBLEUUID hrService(uint16_t(0x180d)), hrMeasurement(uint16_t(0x2a37));

SensorAddress encode(const NimBLEAddress& address) {
    SensorAddress encoded{};
    const auto* base = address.getBase();
    encoded[0] = base->type;
    std::memcpy(encoded.data() + 1, base->val, 6);
    return encoded;
}

NimBLEAddress decode(const SensorAddress& address) {
    // Konstruktor NimBLEAddress(const uint8_t*, type) odwraca kolejność bajtów (reverse_copy),
    // podczas gdy ble_addr_t zachowuje dokładnie porządek z base->val.
    ble_addr_t raw{};
    raw.type = address[0];
    std::memcpy(raw.val, address.data() + 1, 6);
    return NimBLEAddress(raw);
}

bool containsIgnoreCase(const char* haystack, const char* needle) {
    const size_t nlen = std::strlen(needle);
    for (size_t i = 0; haystack[i]; ++i) {
        size_t j = 0;
        while (j < nlen && haystack[i + j] &&
               std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                   std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == nlen) return true;
    }
    return false;
}

bool looksLikeHeartRateSensor(const NimBLEAdvertisedDevice* device, const char* name) {
    if (device->isAdvertisingService(hrService)) return true;
    const uint16_t appearance = device->getAppearance();
    if (appearance == 832 || appearance == 833) return true;
    for (const char* tag : {"hrm", "heart", "polar", "garmin", "wahoo", "tickr", "coospo", "magene", "decathlon", "kalenji"}) {
        if (containsIgnoreCase(name, tag)) return true;
    }
    return false;
}

// Adapter NimBLE/FreeRTOS. Decyzje podejmuje RadioLogic, testowana na hoście.
class NimBleRadio final : public RadioPort {
public:
    void begin();
    void tick(const Request& request);

    uint32_t now() override { return millis(); }
    bool connected() override { return client_->isConnected(); }
    void disconnect() override { client_->disconnect(); }
    bool attach(const SensorAddress& address, uint32_t session) override;
    int scan(ScanEntry* entries, size_t capacity) override;
    bool receive(Sample& sample) override { return xQueueReceive(inbox_, &sample, 0) == pdTRUE; }
    bool forward(const Sample& sample) override { return xQueueSend(hrInput, &sample, 0) == pdTRUE; }
    void overflow() override { hrOverflow.store(true); }
    uint32_t linked() override { return hrSession.load(); }
    void link(uint32_t session) override { hrSession.store(session); }
    void configBusy(bool busy) override { fan::app::configBusy.store(busy); }
    bool configClosed() override { return config_.closed(); }
    void configTick(const Request& request, RadioView& view) override { config_.tick(request, view); }
    bool currentRequest(Request& request) override { return xQueuePeek(radioRequest, &request, 0) == pdTRUE; }
    bool saveSensor(const SensorAddress& address) override { return storage_.saveSensor(address); }
    void publish(const RadioView& view) override { xQueueOverwrite(radioView, &view); }

private:
    Storage storage_;
    ConfigBle config_{storage_};
    NimBLEClient* client_ = nullptr;
    QueueHandle_t inbox_ = nullptr;
    RadioLogic logic_{*this};
};

void NimBleRadio::begin() {
    storage_.begin();
    ConfigView active{};
    active.valid = storage_.load(active.config);
    xQueueOverwrite(configInput, &active);
    if (activeConfigView) xQueueOverwrite(activeConfigView, &active);

    SensorAddress stored{};
    const bool known = storage_.readSensor(stored);

    bool ready = false;
    inbox_ = xQueueCreate(16, sizeof(Sample));
    if (inbox_ && NimBLEDevice::init("SmartFan v2")) {
        ready = config_.begin();
        client_ = NimBLEDevice::createClient();
        if (client_) client_->setConnectTimeout(3000);
        ready = ready && client_;
    }
    logic_.begin(ready, known ? &stored : nullptr);
    radioReady.store(ready);
}

void NimBleRadio::tick(const Request& request) {
    Config pending{};
    if (configSaveRequest && xQueueReceive(configSaveRequest, &pending, 0) == pdTRUE) {
        if (storage_.save(pending)) {
            ConfigView active{pending, true};
            xQueueOverwrite(configInput, &active);
            if (activeConfigView) xQueueOverwrite(activeConfigView, &active);
        }
    }
    logic_.tick(request);
}

bool NimBleRadio::attach(const SensorAddress& address, uint32_t session) {
    // Pasy HR (np. Garmin HRM-Pro+) używają parowania Just Works bez kodu PIN.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    if (!client_->connect(decode(address), true, false, true)) {
        SensorAddress alt = address;
        alt[0] ^= 1;
        if (!client_->connect(decode(alt), true, false, true)) return false;
    }
    auto* service = client_->getService(hrService);
    auto* measurement = service ? service->getCharacteristic(hrMeasurement) : nullptr;
    if (!measurement || (!measurement->canNotify() && !measurement->canIndicate())) return false;
    const bool useNotify = measurement->canNotify();
    auto callback = [queue = inbox_, session](
            NimBLERemoteCharacteristic*, uint8_t* bytes, size_t size, bool) {
        Sample sample{session, millis(), {}};
        if (parseHeartRate(bytes, size, sample.hr) && xQueueSend(queue, &sample, 0) != pdTRUE) {
            hrOverflow.store(true);
        }
    };
    return measurement->subscribe(useNotify, callback, true) ||
           measurement->subscribe(useNotify, callback, false);
}

int NimBleRadio::scan(ScanEntry* entries, size_t capacity) {
    auto* scan = NimBLEDevice::getScan();
    if (!scan) return -1;
    scan->setMaxResults(32);
    scan->setActiveScan(true);
    scan->setInterval(45);
    scan->setWindow(45);
    const auto found = scan->getResults(3000, false);
    int count = 0;
    for (int i = 0; i < found.getCount() && count < int(capacity); ++i) {
        const auto* device = found.getDevice(i);
        if (!device) continue;
        auto& entry = entries[count++];
        entry = {};
        entry.address = encode(device->getAddress());
        if (device->haveName()) std::snprintf(entry.name, sizeof(entry.name), "%s", device->getName().c_str());
        entry.connectable = device->isConnectable();
        entry.heartRate = looksLikeHeartRateSensor(device, entry.name);
    }
    scan->clearResults();
    return count;
}

} // namespace

void radioTask(void*) {
    static NimBleRadio radio;
    radio.begin();
    Request request{};
    for (;;) {
        xQueuePeek(radioRequest, &request, 0);
        radio.tick(request);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace fan::app
