#include "App.h"
#include "Driver.h"
#include "Protocol.h"

namespace fan::app {
namespace {

constexpr uint32_t debounceMs = 20;
constexpr uint32_t holdMs = 1200;

struct Button {
    enum Event : uint8_t { None, Press, Tap, Hold };

    Event update(bool down, uint32_t now) {
        if (raw != down) {
            raw = down;
            changed = now;
        }
        if (down != stable && elapsed(now, changed) >= debounceMs) {
            stable = down;
            if (down) {
                pressed = now;
                consumed = false;
                return Press;
            }
            return consumed ? None : Tap;
        }
        if (stable && !consumed && elapsed(now, pressed) >= holdMs) {
            consumed = true;
            return Hold;
        }
        return None;
    }

    uint32_t changed = 0, pressed = 0;
    bool raw = false, stable = false, consumed = false;
};

} // namespace

void controlTask(void*) {
    Control control;
    Request request{};
    Button pair, config;
    ConfigView loaded{};
    RadioView radio{};
    Snapshot lastSnapshot{};
    uint32_t link = 0;
    bool bootButtonsReleased = false;
    const auto enter = [&](Mode mode, uint32_t now) {
        driver::publish(0);
        request.mode = mode;
        request.since = now;
        ++request.epoch;
        control.select(mode);
    };
    const bool driverReady = driver::begin();
    for (;;) {
        if (xQueueReceive(configInput, &loaded, 0) == pdTRUE) {
            control.configure(loaded.config, loaded.valid, board::drive);
            control.select(request.mode);
        }

        const bool overflow = hrOverflow.exchange(false);
        const uint32_t session = hrSession.load();
        if (link != session) {
            link = session;
            control.link(link);
        }
        if (overflow) control.loseInput();
        Sample sample{};
        for (int i = 0; i < 16 && xQueueReceive(hrInput, &sample, 0) == pdTRUE; ++i) {
            if (!overflow && elapsed(millis(), sample.received) < hrTimeoutMs) {
                control.sample(sample.session, sample.received, sample.hr);
            }
        }
        if (hrSession.load() != link || hrOverflow.load()) control.loseInput();

        const auto now = millis();
        xQueuePeek(radioView, &radio, 0);
        const bool pairDown = digitalRead(board::pair) == LOW;
        const bool configDown = digitalRead(board::config) == LOW;
        if (!pairDown && !configDown) bootButtonsReleased = true;
        const auto p = pair.update(pairDown, now);
        const auto c = config.update(configDown, now);
        if (!bootButtonsReleased) {
            pair.consumed = config.consumed = true;
        } else if (request.mode == Mode::Config) {
            if (p == Button::Press || elapsed(now, request.since) >= wire::windowMs) {
                pair.consumed = true;
                enter(Mode::Off, now);
            } else if (c == Button::Hold) {
                ++request.forget;
                enter(Mode::Off, now);
            }
        } else if (request.mode == Mode::Pair) {
            if (p == Button::Hold || c == Button::Hold || elapsed(now, request.since) >= pairWindowMs ||
                (radio.epoch == request.epoch && radio.state == RadioState::PairSaved)) {
                enter(Mode::Off, now);
            } else if (p == Button::Tap) {
                ++request.next;
            } else if (c == Button::Tap) {
                ++request.confirm;
            }
        } else if (request.mode != Mode::Off) {
            if (p == Button::Press) {
                pair.consumed = config.consumed = true;
                enter(Mode::Off, now);
            } else if (request.mode == Mode::Manual) {
                if (c == Button::Tap) control.changeManual(5);
                if (c == Button::Hold) control.changeManual(-5);
            }
        } else if (p == Button::Hold) {
            enter(Mode::Pair, now);
        } else if (c == Button::Hold) {
            enter(Mode::Config, now);
        } else if (!configBusy.load()) {
            if (p == Button::Tap) enter(Mode::Auto, now);
            else if (c == Button::Tap) enter(Mode::Manual, now);
        }

        UiCommand cmd{};
        if (bootButtonsReleased && uiCommand && xQueueReceive(uiCommand, &cmd, 0) == pdTRUE) {
            if (cmd.action == UiAction::SetMode) {
                if (cmd.mode == Mode::Off || !configBusy.load()) enter(cmd.mode, now);
            } else if (cmd.action == UiAction::SetManual && !configBusy.load()) {
                if (request.mode != Mode::Manual) enter(Mode::Manual, now);
                const uint16_t clamped = cmd.value > 100 ? 100 : cmd.value;
                control.changeManual(int(clamped) - int(lastSnapshot.manual));
            } else if (cmd.action == UiAction::PairSelect && request.mode == Mode::Pair &&
                       radio.epoch == request.epoch && radio.count > 0 && cmd.value < radio.count &&
                       (radio.state == RadioState::Choose || radio.state == RadioState::PairFailed)) {
                const uint32_t delta = (uint32_t(cmd.value) + radio.count - radio.index) % radio.count;
                request.next += delta;
                ++request.confirm;
            }
        }

        const bool initialized = driverReady && uiReady.load() && radioReady.load();
        const auto snapshot = control.tick(now, initialized ? driver::status() : Drive::Fault);
        lastSnapshot = snapshot;
        if (snapshot.mode != request.mode) {
            request.mode = snapshot.mode;
            request.since = now;
            ++request.epoch;
        }
        driver::publish(snapshot.demand);
        digitalWrite(board::led, snapshot.demand != 0);
        xQueueOverwrite(radioRequest, &request);
        xQueueOverwrite(controlView, &snapshot);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

} // namespace fan::app
