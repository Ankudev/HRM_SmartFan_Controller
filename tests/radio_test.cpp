#include "RadioLogic.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <vector>

using namespace fan;
using namespace fan::app;

namespace {

constexpr uint32_t wrap = std::numeric_limits<uint32_t>::max();

SensorAddress address(uint8_t last, uint8_t type = 0) { return {type, last, 2, 3, 4, 5, 6}; }

// Atrapa sprzętu: zegar, łącze BLE, kolejki i NVS. attach może przesuwać zegar jak blokujący connect.
class FakePort final : public RadioPort {
public:
    uint32_t now() override { return clock; }
    bool connected() override { return link_up; }
    void disconnect() override {
        ++disconnects;
        if (drop_on_disconnect) link_up = false;
    }
    bool attach(const SensorAddress& target, uint32_t session) override {
        assert(!link_up);  // jeden klient: nigdy connect przy aktywnym połączeniu
        ++attaches;
        last_target = target;
        last_session = session;
        clock += attach_ms;
        link_up = attach_ok;
        return attach_ok;
    }
    int scan(ScanEntry* entries, size_t capacity) override {
        ++scans;
        clock += 2500;
        if (scan_error) return -1;
        size_t count = 0;
        for (; count < results.size() && count < capacity; ++count) entries[count] = results[count];
        return int(count);
    }
    bool receive(Sample& sample) override {
        if (inbox.empty()) return false;
        sample = inbox.front();
        inbox.pop_front();
        return true;
    }
    bool forward(const Sample& sample) override {
        if (!forward_ok) return false;
        forwarded.push_back(sample);
        return true;
    }
    void overflow() override { ++overflows; }
    uint32_t linked() override { return hr_session; }
    void link(uint32_t session) override { hr_session = session; }
    void configBusy(bool busy) override { busy_flag = busy; }
    bool configClosed() override { return config_closed; }
    void configTick(const Request&, RadioView& view) override {
        ++config_ticks;
        view.state = RadioState::Config;
    }
    bool currentRequest(Request& request) override {
        request = current;
        return true;
    }
    bool saveSensor(const SensorAddress& target) override {
        ++saves;
        if (save_ok) saved = target;
        return save_ok;
    }
    void publish(const RadioView& view) override { states.push_back(view.state); }

    void hr(uint16_t bpm, Contact contact = Contact::Present) {
        inbox.push_back({last_session, clock, {bpm, contact}});
    }

    uint32_t clock = wrap - 20000;
    uint32_t attach_ms = 300;
    uint32_t hr_session = 0;
    uint32_t last_session = 0;
    unsigned attaches = 0, disconnects = 0, scans = 0, overflows = 0, config_ticks = 0, saves = 0;
    bool link_up = false, attach_ok = true, drop_on_disconnect = true, scan_error = false;
    bool forward_ok = true, busy_flag = false, config_closed = true, save_ok = true;
    SensorAddress last_target{}, saved{};
    Request current{};
    std::vector<ScanEntry> results;
    std::deque<Sample> inbox;
    std::vector<Sample> forwarded;
    std::vector<RadioState> states;
};

ScanEntry entry(uint8_t last, const char* name, bool connectable, bool heartRate) {
    ScanEntry result{};
    result.address = address(last);
    std::snprintf(result.name, sizeof(result.name), "%s", name);
    result.connectable = connectable;
    result.heartRate = heartRate;
    return result;
}

void notReady() {
    FakePort port;
    RadioLogic radio(port);
    const auto saved = address(9);
    radio.begin(false, &saved);
    radio.tick({});
    assert(radio.view().state == RadioState::RadioError && port.attaches == 0);
    assert(port.states.back() == RadioState::RadioError);
}

void reconnectAndForward() {
    FakePort port;
    RadioLogic radio(port);
    radio.begin(true, nullptr);
    radio.tick({});
    assert(radio.view().state == RadioState::Idle && port.attaches == 0 && !port.busy_flag);

    const auto saved = address(7);
    FakePort linked;
    RadioLogic link(linked);
    link.begin(true, &saved);
    link.tick({});
    assert(linked.attaches == 1 && linked.last_target == saved);
    assert(linked.hr_session == linked.last_session && linked.hr_session != 0);
    assert(link.view().attempts == 1);
    assert(linked.states.size() >= 2 && linked.states[linked.states.size() - 2] == RadioState::Connecting);

    linked.hr(150);
    linked.inbox.push_back({linked.last_session + 1, linked.clock, {151, Contact::Present}});
    link.tick({});
    assert(link.view().state == RadioState::Subscribed);
    assert(linked.forwarded.size() == 1 && linked.forwarded[0].hr.bpm == 150);

    linked.forward_ok = false;
    linked.hr(152);
    link.tick({});
    assert(linked.overflows == 1);
    std::puts("PASS R01 R02 - start bez radia, reconnect z NVS, generacja sesji, przepelnienie kolejki");
}

void timeoutAndBackoff() {
    FakePort port;
    RadioLogic radio(port);
    const auto saved = address(7);
    radio.begin(true, &saved);
    radio.tick({});
    const uint32_t first = port.last_session;

    // Kontakt Absent odświeża łącze, próbka nieużywalna (bez kontaktu) nie.
    port.clock += RadioLogic::sampleWaitMs - 1;
    port.hr(0, Contact::Absent);
    radio.tick({});
    assert(port.hr_session == first);
    port.clock += RadioLogic::sampleWaitMs - 1;
    port.hr(299, Contact::Present);
    radio.tick({});
    assert(port.hr_session == first);
    port.clock += 1;
    radio.tick({});
    assert(port.hr_session == 0 && port.disconnects == 1 && !port.link_up);

    // Kolejne nieudane próby: odstępy 1, 2, 4, ... 30 s, nigdy connect przy aktywnym łączu.
    port.attach_ok = false;
    port.attach_ms = 3000;
    uint32_t pause = 1000;
    for (int i = 0; i < 1100; ++i) {
        const unsigned before = port.attaches;
        port.clock += pause - 1;
        radio.tick({});
        assert(port.attaches == before);
        port.clock += 1;
        radio.tick({});
        assert(port.attaches == before + 1);
        // Backoff liczony od końca blokującego connect.
        pause = std::min(pause * 2, uint32_t(30000));
    }
    assert(radio.view().attempts == 1101);
    port.attach_ok = true;
    port.clock += 30000;
    radio.tick({});
    assert(port.hr_session == port.last_session && port.last_session != first);
    port.clock += 1000;
    port.hr(140);
    radio.tick({});
    assert(radio.view().state == RadioState::Subscribed);
    std::puts("PASS R03 T06 - timeout 8 s, Absent odswieza lacze, 1100 prob z backoff na kodzie produkcyjnym");
}

void detachRetry() {
    FakePort port;
    RadioLogic radio(port);
    const auto saved = address(7);
    radio.begin(true, &saved);
    radio.tick({});
    port.drop_on_disconnect = false;  // stos BLE nie potwierdza rozłączenia
    Request config{Mode::Config, 1, port.clock, 0, 0, 0};
    radio.tick(config);
    assert(port.busy_flag && port.disconnects == 1 && port.config_ticks == 0 && port.hr_session == 0);
    port.clock += RadioLogic::disconnectRetryMs - 1;
    radio.tick(config);
    assert(port.disconnects == 1);
    port.clock += 1;
    radio.tick(config);
    assert(port.disconnects == 2 && port.config_ticks == 0);
    port.link_up = false;
    radio.tick(config);
    assert(port.config_ticks == 1 && radio.view().state == RadioState::Config);
    assert(radio.view().pin == 1000000);

    // Otwarta sesja CONFIG trzyma ścieżkę konfiguracji także po wyjściu z trybu.
    port.config_closed = false;
    radio.tick({Mode::Off, 2, port.clock, 0, 0, 0});
    assert(port.config_ticks == 2 && port.busy_flag && port.attaches == 1);
    port.config_closed = true;
    radio.tick({Mode::Off, 2, port.clock, 0, 0, 1});
    assert(port.config_ticks == 3);
    radio.tick({Mode::Off, 2, port.clock, 0, 0, 1});
    assert(!port.busy_flag && port.attaches == 2);
    std::puts("PASS R04 T19 - rozlaczenie przed CONFIG, ponowienie co 200 ms, kasowanie par, powrot do lacza");
}

void pairing() {
    FakePort port;
    RadioLogic radio(port);
    radio.begin(true, nullptr);
    port.results = {
        entry(1, "Glosnik", true, false),
        entry(2, "Niepolaczalny HR", false, true),
        entry(3, "HRM-Pro", true, true),
        entry(4, "", true, false),
        entry(5, "Pas\x01\xc5\x82", true, true),
    };
    Request pair{Mode::Pair, 1, port.clock, 0, 0, 0};
    port.current = pair;
    radio.tick(pair);
    assert(port.scans == 1 && radio.view().state == RadioState::Choose && radio.view().count == 4);
    assert(std::strcmp(radio.view().candidate, "HRM-Pro") == 0);
    assert(std::strcmp(radio.view().address, "06:05:04:03:02:03") == 0);
    radio.tick(pair);
    assert(port.scans == 1);

    const char* order[] = {"Pas???", "Glosnik", "(bez nazwy)", "HRM-Pro"};
    for (const char* name : order) {
        ++pair.next;
        port.current = pair;
        radio.tick(pair);
        assert(std::strcmp(radio.view().candidate, name) == 0);
    }
    assert(radio.view().items[0].heartRate && !radio.view().items[2].heartRate);
    // Skok wielokrotny (wybór dowolnego pasa jednym kliknięciem z panelu WWW):
    pair.next += 2;
    port.current = pair;
    radio.tick(pair);
    assert(radio.view().index == 2 && std::strcmp(radio.view().candidate, "Glosnik") == 0);
    pair.next += 2;
    port.current = pair;
    radio.tick(pair);
    assert(radio.view().index == 0 && std::strcmp(radio.view().candidate, "HRM-Pro") == 0);

    ++pair.confirm;
    port.current = pair;
    radio.tick(pair);
    assert(port.attaches == 1 && port.last_target == address(3));
    assert(port.saves == 1 && port.saved == address(3) && radio.view().state == RadioState::PairSaved);
    assert(port.hr_session == port.last_session);

    // Sterowanie wraca do OFF: aktywne połączenie z nowym pasem zostaje utrzymane bez rozłączania.
    Request off{Mode::Off, 2, port.clock, pair.next, pair.confirm, 0};
    radio.tick(off);
    assert(port.link_up && port.attaches == 1 && radio.view().state == RadioState::Subscribed);
    std::puts("PASS R05 - skan HR najpierw, filtr, etykiety, wybor, weryfikacja uslugi HR i zapis pasa");
}

void pairingFailures() {
    FakePort port;
    RadioLogic radio(port);
    radio.begin(true, nullptr);
    port.results = {entry(3, "HRM-Pro", true, true)};
    Request pair{Mode::Pair, 1, port.clock, 0, 1, 0};
    port.current = pair;
    radio.tick({Mode::Pair, 1, port.clock, 0, 0, 0});

    // Błąd zapisu NVS podczas parowania.
    port.save_ok = false;
    radio.tick(pair);
    assert(port.saves == 1 && radio.view().state == RadioState::PairFailed && !port.link_up);

    // Nieudane połączenie z kandydatem (brak usługi 0x180D / odrzucenie BLE).
    ++pair.confirm;
    port.current = pair;
    port.save_ok = true;
    port.attach_ok = false;
    radio.tick(pair);
    assert(radio.view().state == RadioState::PairFailed && port.saves == 1 && !port.link_up);

    FakePort empty;
    RadioLogic none(empty);
    none.begin(true, nullptr);
    none.tick({Mode::Pair, 1, empty.clock, 0, 0, 0});
    assert(none.view().state == RadioState::NoCandidates && none.view().count == 0);
    none.tick({Mode::Pair, 1, empty.clock, 1, 1, 0});
    assert(none.view().state == RadioState::NoCandidates && empty.attaches == 0);

    FakePort broken;
    RadioLogic error(broken);
    broken.scan_error = true;
    error.begin(true, nullptr);
    error.tick({Mode::Pair, 1, broken.clock, 0, 0, 0});
    assert(error.view().state == RadioState::RadioError);

    FakePort many;
    RadioLogic limit(many);
    for (uint8_t i = 0; i < 40; ++i) many.results.push_back(entry(i, "X", true, i % 2));
    limit.begin(true, nullptr);
    limit.tick({Mode::Pair, 1, many.clock, 0, 0, 0});
    assert(limit.view().count == RadioLogic::maxCandidates);
    std::puts("PASS R06 - brak probki 8 s, blad NVS, wygasle okno, nieudane laczenie, pusty skan, limit");
}

} // namespace

void radioTests() {
    notReady();
    reconnectAndForward();
    timeoutAndBackoff();
    detachRetry();
    pairing();
    pairingFailures();
}
