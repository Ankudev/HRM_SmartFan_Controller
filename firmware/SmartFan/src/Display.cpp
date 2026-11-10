#include <LovyanGFX.hpp>

#include <cstdio>

#include "App.h"
#include "Board.h"

#ifndef FAN_BUILD_ID
#define FAN_BUILD_ID "lokalny"
#endif

namespace fan::app {
namespace {

class Screen final : public lgfx::LGFX_Device {
public:
    Screen() {
        auto spi = bus_.config();
        spi.spi_host = SPI2_HOST;
        spi.spi_mode = 0;
        spi.freq_write = 27000000;
        spi.pin_sclk = board::sck;
        spi.pin_mosi = board::mosi;
        spi.pin_miso = -1;
        spi.pin_dc = board::dc;
        bus_.config(spi);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_cs = board::cs;
        panel.pin_rst = board::reset;
        panel.memory_width = panel.panel_width = 240;
        panel.memory_height = panel.panel_height = 240;
        panel.readable = false;
        panel.invert = true;
        panel_.config(panel);
        setPanel(&panel_);
    }

private:
    lgfx::Bus_SPI bus_;
    lgfx::Panel_GC9A01 panel_;
};

Screen screen;
LGFX_Sprite sprite(&screen);

const char* modeText(Mode mode) {
    switch (mode) {
    case Mode::Auto: return "AUTO";
    case Mode::Manual: return "MANUAL";
    case Mode::Pair: return "WYBOR PASA";
    case Mode::Config: return "KONFIGURACJA";
    case Mode::Off: return "OFF";
    }
    return "";
}

const char* reasonText(Reason reason) {
    switch (reason) {
    case Reason::Off: return "Wylaczony";
    case Reason::Running: return "Sterowanie";
    case Reason::Waiting: return "Czekam na 2 probki";
    case Reason::Stale: return "Brak swiezego HR";
    case Reason::NoContact: return "Brak kontaktu pasa";
    case Reason::NoLink: return "Brak polaczenia";
    case Reason::Profile: return "Profil AC niezatw.";
    case Reason::InvalidConfig: return "Ustaw konfiguracje";
    case Reason::Sync: return "Czekam na Z-C";
    case Reason::Fault: return "Blad inicj. / napedu";
    case Reason::Pair: return "PAIR: nastepny";
    case Reason::Config: return "PAIR: wyjscie";
    }
    return "";
}

const char* radioText(RadioState state) {
    switch (state) {
    case RadioState::Connecting: return "Laczenie z pasem";
    case RadioState::Subscribed: return "Pas polaczony";
    case RadioState::Scanning: return "Szukam urzadzen";
    case RadioState::Choose: return "CONFIG: wybierz";
    case RadioState::Checking: return "Sprawdzam pas";
    case RadioState::NoCandidates: return "Brak kandydatow";
    case RadioState::PairFailed: return "Nie zweryfikowano";
    case RadioState::PairSaved: return "Zapisano pas";
    case RadioState::Config: return "BLE: SmartFan v2";
    case RadioState::RadioError: return "Blad BLE";
    case RadioState::BondsRemoved: return "Usunieto parowania";
    case RadioState::BondError: return "Blad usuwania par";
    default: return "Pas niepolaczony";
    }
}

} // namespace

bool displayBegin() {
    pinMode(board::backlight, OUTPUT);
    digitalWrite(board::backlight, LOW);
    if (!screen.init()) return false;
    // 8-bitowy bufor ogranicza zużycie wewnętrznej RAM bez PSRAM.
    sprite.setColorDepth(8);
    if (!sprite.createSprite(240, 240)) return false;
    sprite.setTextDatum(middle_center);
    digitalWrite(board::backlight, HIGH);
    return true;
}

void displayUpdate() {
    Snapshot state{};
    RadioView radio{};
    if (xQueuePeek(controlView, &state, 0) != pdTRUE) return;
    xQueuePeek(radioView, &radio, 0);

    char text[48]{};
    sprite.fillScreen(TFT_BLACK);
    sprite.setTextColor(TFT_CYAN);
    sprite.drawString(modeText(state.mode), 120, 42, &lgfx::fonts::Font2);
    sprite.setTextColor(TFT_WHITE);
    if (state.mode == Mode::Config) {
        if (radio.pin < 1000000) std::snprintf(text, sizeof(text), "%06lu", static_cast<unsigned long>(radio.pin));
        else std::snprintf(text, sizeof(text), "BLE");
        sprite.drawString(text, 120, 87, &lgfx::fonts::Font4);
        sprite.drawString(radioText(radio.state), 120, 121, &lgfx::fonts::Font2);
        sprite.drawString("Przytrz. CONFIG: usun pary", 120, 152, &lgfx::fonts::Font0);
    } else if (state.mode == Mode::Pair) {
        sprite.drawString(radioText(radio.state), 120, 78, &lgfx::fonts::Font2);
        if (radio.count && (radio.state == RadioState::Choose || radio.state == RadioState::PairFailed)) {
            std::snprintf(text, sizeof(text), "%u/%u %s", unsigned(radio.index + 1),
                          unsigned(radio.count), radio.candidate);
            sprite.drawString(text, 120, 112, &lgfx::fonts::Font0);
            sprite.drawString(radio.address, 120, 132, &lgfx::fonts::Font0);
            sprite.drawString("CONFIG: wybierz", 120, 151, &lgfx::fonts::Font0);
        }
    } else {
        if (state.fresh) std::snprintf(text, sizeof(text), "%u BPM", unsigned(state.bpm));
        else std::snprintf(text, sizeof(text), "-- BPM");
        sprite.drawString(text, 120, 82, &lgfx::fonts::Font4);
        if (state.mode == Mode::Manual) {
            std::snprintf(text, sizeof(text), "Nastawa %u%% / cel %u%%",
                          unsigned(state.demand), unsigned(state.manual));
        } else {
            std::snprintf(text, sizeof(text), "Nastawa %u%%", unsigned(state.demand));
        }
        sprite.drawString(text, 120, 117, &lgfx::fonts::Font2);
        sprite.drawString(radioText(radio.state), 120, 142, &lgfx::fonts::Font2);
    }
    sprite.setTextColor(TFT_YELLOW);
    sprite.drawString(reasonText(state.reason), 120, 168, &lgfx::fonts::Font2);
    sprite.setTextColor(TFT_LIGHTGREY);
    if (state.mode == Mode::Off) {
        sprite.drawString("PAIR:Auto  CFG:Manual", 120, 190, &lgfx::fonts::Font0);
    } else if (state.mode == Mode::Manual) {
        sprite.drawString("PAIR:Off  CFG:+5/przytrz.-5", 120, 190, &lgfx::fonts::Font0);
    } else if (state.mode == Mode::Auto) {
        sprite.drawString("PAIR:Off  WiFi:SmartFan", 120, 190, &lgfx::fonts::Font0);
    } else {
        sprite.drawString("WiFi: SmartFan 192.168.4.1", 120, 190, &lgfx::fonts::Font0);
    }
#if FAN_AC_HARDWARE
    constexpr char profile[] = "AC";
#else
    constexpr char profile[] = "DRY-RUN";
#endif
    std::snprintf(text, sizeof(text), "%s %.12s", profile, FAN_BUILD_ID);
    sprite.drawString(text, 120, 208, &lgfx::fonts::Font0);
    sprite.pushSprite(0, 0);
}

} // namespace fan::app
