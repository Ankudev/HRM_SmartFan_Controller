#pragma once

// Maszyna stanów radia (pas HR, wybór pasa, okno CONFIG) bez zależności od NimBLE,
// FreeRTOS i Arduino. Sprzęt i kolejki są dostępne wyłącznie przez RadioPort, dzięki
// czemu ten sam kod jest kompilowany w firmware i w testach hostowych.

#include <array>
#include <cstddef>
#include <cstdint>

#include "Core.h"

namespace fan::app {

enum class RadioState : uint8_t {
    Boot, Idle, Connecting, Subscribed, Scanning, Choose, Checking,
    NoCandidates, PairFailed, PairSaved, Config, RadioError, BondsRemoved, BondError
};

struct Request {
    Mode mode = Mode::Off;
    uint32_t epoch = 0;
    uint32_t since = 0;
    uint32_t next = 0;
    uint32_t confirm = 0;
    uint32_t forget = 0;
};

struct CandidateInfo {
    char label[25]{};
    char address[18]{};
    bool heartRate = false;
};

struct RadioView {
    RadioState state = RadioState::Boot;
    uint32_t epoch = 0;
    uint32_t pin = 1000000;
    uint32_t attempts = 0;
    uint8_t index = 0;
    uint8_t count = 0;
    char candidate[25]{};
    char address[18]{};
    CandidateInfo items[12]{};
};

struct Sample {
    uint32_t session;
    uint32_t received;
    HeartRate hr;
};

inline constexpr uint32_t pairWindowMs = 60000;

// [0] typ adresu BLE, [1..6] bajty adresu od najmłodszego (format rekordu NVS).
using SensorAddress = std::array<uint8_t, 7>;

struct ScanEntry {
    SensorAddress address{};
    char name[25]{};  // pusty napis oznacza brak nazwy
    bool connectable = false;
    bool heartRate = false;
};

// Operacje sprzętowe wykonywane przez maszynę stanów. attach i scan blokują wywołującego.
class RadioPort {
public:
    virtual uint32_t now() = 0;
    virtual bool connected() = 0;
    virtual void disconnect() = 0;
    // Łączy, sprawdza usługę HR i subskrybuje pomiar. Próbki trafiają do receive() z tym numerem sesji.
    virtual bool attach(const SensorAddress& address, uint32_t session) = 0;
    // Wypełnia entries wynikami skanowania. Wartość ujemna oznacza brak skanera.
    virtual int scan(ScanEntry* entries, size_t capacity) = 0;
    virtual bool receive(Sample& sample) = 0;
    virtual bool forward(const Sample& sample) = 0;
    virtual void overflow() = 0;
    virtual uint32_t linked() = 0;
    virtual void link(uint32_t session) = 0;
    virtual void configBusy(bool busy) = 0;
    virtual bool configClosed() = 0;
    virtual void configTick(const Request& request, RadioView& view) = 0;
    virtual bool currentRequest(Request& request) = 0;
    virtual bool saveSensor(const SensorAddress& address) = 0;
    virtual void publish(const RadioView& view) = 0;

protected:
    ~RadioPort() = default;
};

class RadioLogic {
public:
    static constexpr size_t maxCandidates = 12;
    static constexpr size_t maxScan = 32;
    static constexpr uint32_t sampleWaitMs = 8000;
    static constexpr uint32_t disconnectRetryMs = 200;

    explicit RadioLogic(RadioPort& port) : port_(port) {}
    void begin(bool ready, const SensorAddress* saved);
    void tick(const Request& request);
    const RadioView& view() const { return view_; }

private:
    struct Candidate {
        SensorAddress address{};
        char label[25]{};
    };

    bool detach();
    bool attach(const SensorAddress& address);
    void pollSamples(const Request& request);
    void scan();
    void publishCandidate();
    void pairTick(const Request& request, bool changed);
    void linkTick(const Request& request, uint32_t now);

    RadioPort& port_;
    Backoff backoff_;
    RadioView view_{};
    SensorAddress saved_{};
    Candidate candidates_[maxCandidates]{};
    ScanEntry found_[maxScan]{};
    uint32_t session_ = 0;
    uint32_t nextSeen_ = 0;
    uint32_t confirmSeen_ = 0;
    uint32_t heardAt_ = 0;
    uint32_t checkingSince_ = 0;
    uint32_t disconnectAt_ = 0;
    uint32_t forgotten_ = 0;
    bool disconnectPending_ = false;
    bool selecting_ = false;
    bool paired_ = false;
    bool savedKnown_ = false;
    bool ready_ = false;
};

void formatAddress(const SensorAddress& address, char (&text)[18]);

} // namespace fan::app

