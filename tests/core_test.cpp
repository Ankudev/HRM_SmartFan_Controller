#include "Core.h"
#include "Phase.h"
#include "Protocol.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

using namespace fan;
namespace w = fan::wire;

namespace {

constexpr DriveProfile testDrive{true, 30, 50, 0, 0, 350, 8000, 100, false};
constexpr uint32_t wrap = std::numeric_limits<uint32_t>::max();

void hrPair(Control& c, uint32_t session, uint32_t start, uint16_t bpm) {
    c.sample(session, start, {bpm, Contact::Unknown});
    c.sample(session, start + 100, {bpm, Contact::Present});
}

void bootAndModes() {
    Control c;
    Config config;
    c.configure(config, false, testDrive);
    assert(c.tick(1, Drive::Ready).reason == Reason::InvalidConfig);
    c.select(Mode::Manual);
    assert(c.tick(2, Drive::Ready).demand == 0);
    c.configure(config, true, {});
    assert(c.tick(3, Drive::Ready).reason == Reason::Profile);
    c.configure(config, true, testDrive);
    assert(c.tick(4, Drive::Ready).mode == Mode::Off);
    assert(c.tick(4, Drive::Ready).demand == 0);
    c.select(Mode::Manual);
    assert(c.tick(5, Drive::Ready).demand == 30);
    c.select(Mode::Auto);
    assert(c.tick(6, Drive::Ready).reason == Reason::NoLink);
    c.link(1);
    c.sample(1, 10, {165, Contact::Unknown});
    assert(c.tick(10, Drive::Ready).reason == Reason::Waiting);
    c.sample(1, 11, {165, Contact::Present});
    assert(c.tick(11, Drive::Ready).demand == 30);
    c.select(Mode::Manual);
    c.unlink(1);
    assert(c.tick(12, Drive::Ready).demand == 30);
    c.select(Mode::Config);
    assert(c.tick(13, Drive::Ready).reason == Reason::Config);
    c.select(Mode::Off);
    c.link(2);
    hrPair(c, 2, 20, 180);
    assert(c.tick(120, Drive::Ready).demand == 0);
    c.select(Mode::Auto);
    assert(c.tick(121, Drive::Ready).demand == 30);
    assert(c.tick(122, Drive::Waiting).reason == Reason::Fault);
    assert(c.tick(123, Drive::Ready).mode == Mode::Off);
    c.select(Mode::Manual);
    assert(c.tick(124, Drive::Fault).reason == Reason::Fault);
    c.configure(config, true, testDrive);
    assert(c.tick(125, Drive::Ready).demand == 0);
    std::puts("PASS T01 T11 T12 T19 - boot, konfiguracja, AUTO/MANUAL/OFF");
}

void parser() {
    HeartRate hr;
    const uint8_t simple[]{0, 150};
    const uint8_t optional[]{31, 200, 0, 1, 2, 3, 4, 5, 6};
    assert(!parseHeartRate(nullptr, 0, hr));
    assert(parseHeartRate(simple, sizeof(simple), hr));
    assert(hr.bpm == 150 && hr.contact == Contact::Unknown);
    assert(parseHeartRate(optional, sizeof(optional), hr));
    assert(hr.bpm == 200 && hr.contact == Contact::Present);
    for (size_t i = 0; i < sizeof(optional); ++i) {
        assert(parseHeartRate(optional, i, hr) == (i >= 7));
    }
    const uint8_t absent[]{4, 123};
    const uint8_t unsupported[]{2, 123};
    const uint8_t wide[]{1, 0x2b, 0x01};
    const uint8_t extra[]{0, 80, 42};
    assert(parseHeartRate(absent, sizeof(absent), hr) && !usable(hr));
    assert(hr.contact == Contact::Absent);
    assert(parseHeartRate(unsupported, sizeof(unsupported), hr) && usable(hr));
    assert(hr.contact == Contact::Unknown);
    assert(parseHeartRate(wide, sizeof(wide), hr) && hr.bpm == 299 && !usable(hr));
    assert(parseHeartRate(extra, sizeof(extra), hr) && hr.bpm == 80 && usable(hr));
    std::puts("PASS T02 T03 - format HR, energia, RR, kontakt");
}

void freshness() {
    Control c;
    c.configure(Config{}, true, testDrive);
    c.select(Mode::Auto);
    c.link(3);
    hrPair(c, 3, wrap - 400, 160);
    assert(c.tick(wrap - 300, Drive::Ready).demand == 30);
    c.sample(3, 4100, {255, Contact::Unknown});
    assert(c.tick(4698, Drive::Ready).fresh);
    const auto stale = c.tick(4699, Drive::Ready);
    assert(!stale.fresh && !stale.demand && stale.reason == Reason::Stale);
    c.sample(3, 5000, {150, Contact::Present});
    assert(c.tick(5000, Drive::Ready).demand == 0);
    c.sample(3, 5001, {150, Contact::Absent});
    assert(c.tick(5001, Drive::Ready).reason == Reason::NoContact);

    c.link(4);
    hrPair(c, 3, 5100, 180);
    c.unlink(3);
    assert(c.tick(5200, Drive::Ready).linked && !c.tick(5200, Drive::Ready).fresh);
    hrPair(c, 4, 5210, 180);
    assert(c.tick(5310, Drive::Ready).demand == 30);
    c.loseInput();
    assert(!c.tick(5311, Drive::Ready).demand);
    c.sample(4, 5312, {180, Contact::Unknown});
    assert(!c.tick(5312, Drive::Ready).demand);
    c.select(Mode::Off);
    c.unlink(4);
    c.link(5);
    hrPair(c, 5, 5400, 220);
    assert(!c.tick(5500, Drive::Ready).demand);
    std::puts("PASS T04 T05 T12 T16 - timeout, rollover, generacja i odrzut kolejki");
}

uint8_t steady(Config config, uint16_t bpm) {
    Control c;
    constexpr DriveProfile wider{true, 30, 100, 0, 0, 350, 8000, 100, false};
    c.configure(config, true, wider);
    c.select(Mode::Auto);
    c.link(1);
    hrPair(c, 1, 100, bpm);
    Snapshot s{};
    for (uint32_t now = 200; now <= 4000; now += 20) s = c.tick(now, Drive::Ready);
    return s.demand;
}

void rampAndBounds() {
    auto startup = testDrive;
    startup.startDemand = 50;
    startup.startMs = 300;
    Config config;
    config.manual = 30;
    Control c;
    c.configure(config, true, startup);
    c.select(Mode::Manual);
    assert(c.tick(1, Drive::Ready).demand == 50);
    c.select(Mode::Off);
    assert(!c.tick(2, Drive::Ready).demand);
    c.select(Mode::Manual);
    c.changeManual(-30);
    assert(c.tick(3, Drive::Ready).demand == 0);
    c.configure(config, true, testDrive);
    c.select(Mode::Manual);
    assert(c.tick(4, Drive::Ready).demand == 30);
    c.changeManual(-30);
    assert(c.tick(5, Drive::Ready).demand == 0);
    startup.startDemand = 51;
    assert(!valid(startup));

    config.risePerSecond = 100;
    config.fallPerSecond = 10;
    for (const int upper : {200, 225}) {
        config.threshold.back() = uint16_t(upper);
        uint8_t previous = 0;
        for (uint16_t bpm = 25; bpm <= 250; ++bpm) {
            const auto output = steady(config, bpm);
            assert(output >= previous && output <= 100);
            previous = output;
        }
        assert(steady(config, upper - 1) == 75);
        assert(steady(config, upper) == 95);
        assert(steady(config, upper + 1) == 95);
    }
    for (size_t i = 0; i < zoneCount; ++i) {
        assert(steady(config, config.threshold[i]) == config.demand[i]);
        assert(steady(config, config.threshold[i] - 1) == (i ? config.demand[i - 1] : 0));
    }

    config.manual = 50;
    Control fine, rough;
    fine.configure(config, true, testDrive);
    rough.configure(config, true, testDrive);
    for (auto* item : {&fine, &rough}) {
        item->tick(wrap - 51, Drive::Ready);
        item->select(Mode::Manual);
        assert(item->tick(wrap - 50, Drive::Ready).demand == 30);
    }
    for (uint32_t delta = 10; delta <= 100; delta += 10) fine.tick(wrap - 50 + delta, Drive::Ready);
    assert(fine.tick(49, Drive::Ready).demand == rough.tick(49, Drive::Ready).demand);
    assert(rough.tick(49, Drive::Ready).demand == 40);
    rough.changeManual(-100);
    assert(rough.tick(6000, Drive::Ready).demand == 0);

    Control slow;
    config.risePerSecond = 20;
    slow.configure(config, true, testDrive);
    slow.tick(100, Drive::Ready);
    slow.select(Mode::Manual);
    assert(slow.tick(101, Drive::Ready).demand == 30);
    assert(slow.tick(9000, Drive::Ready).demand == 32);

    Control hysteresis;
    config.filterMs = 0;
    hysteresis.configure(config, true, testDrive);
    hysteresis.select(Mode::Auto);
    hysteresis.link(1);
    hrPair(hysteresis, 1, 1, 105);
    assert(hysteresis.tick(101, Drive::Ready).demand == 30);
    hysteresis.sample(1, 200, {103, Contact::Present});
    assert(hysteresis.tick(200, Drive::Ready).demand == 30);
    hysteresis.sample(1, 201, {101, Contact::Present});
    assert(hysteresis.tick(201, Drive::Ready).demand == 0);

    // Asymetryczny filtr HR (Fast Attack / Slow Decay): szybkie wejście w strefę, wolne zejście.
    Control asym;
    Config asymCfg;
    asymCfg.filterMs = 2000;
    asymCfg.risePerSecond = 100;
    asymCfg.fallPerSecond = 100;
    asym.configure(asymCfg, true, testDrive);
    asym.select(Mode::Auto);
    asym.link(1);
    hrPair(asym, 1, 100, 100);
    assert(asym.tick(200, Drive::Ready).demand == 0);
    asym.sample(1, 700, {115, Contact::Present});  // skok w górę: tau = 500 ms -> przekracza 105 BPM
    assert(asym.tick(700, Drive::Ready).demand == 30);
    asym.sample(1, 1200, {95, Contact::Present});  // spadek w dół: tau = 4000 ms -> trzyma strefę Z1
    assert(asym.tick(1200, Drive::Ready).demand == 30);
    std::puts("PASS T07 T08 T10 T13 - zero, progi, histereza, limity i czas");
}

void acquire(Phase& phase, uint64_t base) {
    phase.configure(testDrive);
    for (unsigned i = 0; i <= 3; ++i) {
        assert(phase.edge(base + i * 10000));
        assert(phase.status(base + i * 10000) == (i == 3 ? Drive::Ready : Drive::Waiting));
        assert(!phase.due());
    }
}

void phaseTiming() {
    Phase p;
    const uint64_t base = uint64_t(wrap) - 12000;
    acquire(p, base);
    const auto first = base + 40000;
    assert(p.publish(base + 30500, 50));
    assert(!p.edge(base + 31000));
    assert(!p.due());
    assert(p.edge(first));
    assert(p.due() == first + testDrive.earliestUs);
    assert(!p.alarm(p.due() - 1));
    assert(p.alarm(p.due()) && p.gate());
    assert(p.alarm(p.due()) && !p.gate() && !p.due());
    const auto next = first + 10000;
    assert(p.publish(next - 5000, 30));
    assert(p.edge(next) && p.due() == next + testDrive.latestUs);
    const auto old = p.due();
    assert(!p.publish(next + 1, 0));
    assert(p.publish(next + 2, 40));
    assert(!p.alarm(old) && !p.gate() && !p.due());
    // Pojedynczy brakujący impuls Z-C (20 ms <= 22 ms) podbija licznik miękki, ale utrzymuje Ready.
    assert(p.status(next + 12000) == Drive::Ready);
    assert(p.edge(next + 20000) && p.due() > 0 && p.softFaults() == 1);
    assert(p.status(next + 20000) == Drive::Ready);
    // Całkowity zanik Z-C (> 22 ms) natychmiast zatrzaskuje usterkę.
    assert(p.edge(next + 45000) && !p.due());
    assert(p.status(next + 45000) == Drive::Fault);
    for (unsigned i = 1; i <= 4; ++i) assert(p.edge(next + 45000 + i * 10000) && !p.due());

    acquire(p, 0);
    assert(p.publish(30100, 50));
    for (uint64_t edge : {40000, 50000, 60000}) {
        assert(p.edge(edge));
        if (edge < 60000) {
            assert(p.alarm(p.due()) && p.gate());
            assert(p.alarm(p.due()) && !p.gate());
        }
    }
    assert(p.alarm(p.due()) && !p.gate());
    assert(p.status(60500) == Drive::Fault);
    assert(!p.publish(60500, 50));

    // Spóźniony alarm (> 100 us) pomija pojedynczy impuls, zanika po 100 czystych półokresach,
    // a 4 błędy miękkie z rzędu zatrzaskują Drive::Fault.
    acquire(p, 0);
    uint64_t t = 30000;
    assert(p.publish(t + 1, 50));
    t += 10000;
    assert(p.edge(t));
    assert(p.alarm(p.due() + 101));
    assert(p.status(t + 500) == Drive::Ready && !p.gate() && p.softFaults() == 1);
    for (unsigned i = 0; i < Phase::cleanDecayHalves; ++i) {
        assert(p.publish(t + 1000, 50));
        t += 10000;
        assert(p.edge(t));
        assert(p.alarm(p.due()) && p.gate());
        assert(p.alarm(p.due()) && !p.gate());
    }
    assert(p.softFaults() == 0);
    for (unsigned i = 1; i <= Phase::softFaultLimit; ++i) {
        assert(p.publish(t + 1000, 50));
        t += 10000;
        assert(p.edge(t));
        assert(p.alarm(p.due() + 101) && !p.gate());
        assert(p.softFaults() == i);
    }
    assert(p.status(t + 500) == Drive::Fault && !p.publish(t + 500, 50));

    p.configure({});
    for (unsigned i = 0; i <= 5; ++i) {
        assert(p.edge(i * 10000));
        assert(!p.publish(i * 10000, 50));
        assert(!p.due());
    }
    std::puts("PASS T14 T15 - filtr Z-C, synchronizacja, stary alarm, dzierzawa i 64-bit");
}

class MemoryStore final : public w::Store {
public:
    bool load(Config& c) override { return present && w::decodeRecord(record, c); }
    bool save(const Config& c) override {
        ++attempts;
        if (!valid(c)) return false;
        if (fail) return false;
        record = w::encodeRecord(c);
        present = true;
        return true;
    }

    w::Record record{};
    unsigned attempts = 0;
    bool present = false;
    bool fail = false;
};

w::Frame command(w::Op op, uint32_t id, Config* c = nullptr) {
    auto data = c ? encode(*c) : ConfigBytes{};
    return w::encodeFrame(uint8_t(op), id, data.data(), c ? data.size() : 0);
}

void code(const w::Frame& f, w::Result r) {
    assert(f.size && f.length() && f.payload()[0] == uint8_t(r));
}

void protocol() {
    MemoryStore store;
    w::Session session(store);
    const uint32_t now = wrap - 100;
    session.open(now);
    auto get = command(w::Op::Get, 1);
    bool committed = false;
    assert(!session.handle(get, now, false, committed).size);
    assert(!session.handle(get, now + w::windowMs, true, committed).size);
    code(session.handle(get, now, true, committed), w::Result::Empty);
    Config config, copy;
    assert(store.save(config));
    store.attempts = 0;
    config.manual = 70;
    auto set = command(w::Op::Set, 2, &config);
    auto save = command(w::Op::Save, 3);
    w::Stream stream;
    unsigned frames = 0;
    for (size_t i = 0; i < 20; ++i) assert(!stream.push(set.bytes[i], now));
    for (size_t i = 20; i < set.size; ++i) {
        if (stream.push(set.bytes[i], now + 1)) {
            ++frames;
            code(session.handle(stream.frame(), now + 1, true, committed), w::Result::Ok);
            stream.reset();
        }
    }
    assert(frames == 1 && !committed);
    assert(store.load(copy) && copy.manual == 45);
    code(session.handle(set, now + 2, true, committed), w::Result::Ok);
    code(session.handle(save, now + 3, true, committed), w::Result::Ok);
    assert(committed && store.attempts == 1 && store.load(copy) && copy.manual == 70);
    code(session.handle(save, now + 4, true, committed), w::Result::Ok);
    assert(!committed && store.attempts == 1);
    code(session.handle(set, now + 5, true, committed), w::Result::Sequence);

    session.open(now);
    for (size_t i = 0; i < 20; ++i) assert(!stream.push(set.bytes[i], now));
    stream.expire(now + w::fragmentMs);
    for (size_t i = 20; i < set.size; ++i) assert(!stream.push(set.bytes[i], now + w::fragmentMs));
    auto corrupt = set;
    corrupt.bytes.back() ^= 1;
    corrupt.bytes[corrupt.size - 1] ^= 1;
    for (size_t i = 0; i < corrupt.size; ++i) assert(!stream.push(corrupt.bytes[i], now + w::fragmentMs));
    code(session.handle(save, now + w::fragmentMs, true, committed), w::Result::NoCandidate);
    assert(store.attempts == 1);
    frames = 0;
    for (auto packet : {command(w::Op::Get, 4), command(w::Op::Close, 5)}) {
        for (size_t i = 0; i < packet.size; ++i) {
            if (stream.push(packet.bytes[i], now + w::fragmentMs + 1)) {
                code(session.handle(stream.frame(), now + w::fragmentMs + 1, true, committed), w::Result::Ok);
                ++frames;
                stream.reset();
            }
        }
    }
    assert(frames == 2);

    for (unsigned violation = 0; violation < 5; ++violation) {
        auto bad = config;
        if (violation == 0) bad.threshold[2] = bad.threshold[1] + 1;
        if (violation == 1) bad.threshold[2] = bad.threshold[1];
        if (violation == 2) bad.threshold[3] = bad.threshold[0];
        if (violation == 3) bad.manual = uint16_t(-1);
        if (violation == 4) bad.demand[4] = 101;
        auto invalid = command(w::Op::Set, 6 + 2 * violation, &bad);
        auto after = command(w::Op::Save, 7 + 2 * violation);
        code(session.handle(invalid, now + 100, true, committed), w::Result::Range);
        code(session.handle(after, now + 101, true, committed), w::Result::NoCandidate);
        assert(store.load(copy) && copy.manual == 70);
    }

    config.manual = 80;
    code(session.handle(command(w::Op::Set, 20, &config), now + 200, true, committed), w::Result::Ok);
    store.fail = true;
    code(session.handle(command(w::Op::Save, 21), now + 201, true, committed), w::Result::Storage);
    assert(!committed && store.load(copy) && copy.manual == 70);
    session.close();
    assert(!session.handle(command(w::Op::Save, 22), now + 202, true, committed).size);
    session.open(now + 300);
    code(session.handle(command(w::Op::Save, 1), now + 300, true, committed), w::Result::NoCandidate);
    auto torn = store.record;
    torn[5] ^= 7;
    assert(!w::decodeRecord(torn, copy));
    std::puts("PASS T09 T17 T18 T20 - ramki, staging, powtorzenia, autoryzacja i model NVS");
}

} // namespace

void radioTests();

int main(int argc, char**) {
    if (argc > 1) {
        Config defaults;
        const auto frame = command(w::Op::Set, 42, &defaults);
        for (size_t i = 0; i < frame.size; ++i) std::printf("%02x", unsigned(frame.bytes[i]));
        std::puts("");
        return 0;
    }
    bootAndModes();
    parser();
    freshness();
    rampAndBounds();
    phaseTiming();
    protocol();
    radioTests();
}
