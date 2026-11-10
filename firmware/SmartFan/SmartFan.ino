#include <Arduino.h>

#include "src/App.h"
#include "src/Board.h"
#include "src/Driver.h"

#ifndef FAN_BUILD_ID
#define FAN_BUILD_ID "lokalny"
#endif

using namespace fan;

namespace {

TaskHandle_t controlHandle = nullptr;
TaskHandle_t radioHandle = nullptr;

unsigned stackLeft(TaskHandle_t task) {
    // W ESP-IDF stos jest liczony w bajtach.
    return task ? unsigned(uxTaskGetStackHighWaterMark(task)) : 0;
}

// Linia diagnostyczna dla protokołu H01: sterta, zapas stosów, stan sterowania i radia.
void printDiagnostics() {
    Snapshot state{};
    app::RadioView radio{};
    if (app::controlView) xQueuePeek(app::controlView, &state, 0);
    if (app::radioView) xQueuePeek(app::radioView, &radio, 0);
    Serial.printf("diag t=%lu heap=%lu min=%lu stk_ctl=%u stk_ble=%u stk_loop=%u mode=%u reason=%u "
                  "radio=%u attempts=%lu\n",
                  static_cast<unsigned long>(millis()), static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()), stackLeft(controlHandle),
                  stackLeft(radioHandle), stackLeft(xTaskGetCurrentTaskHandle()), unsigned(state.mode),
                  unsigned(state.reason), unsigned(radio.state), static_cast<unsigned long>(radio.attempts));
}

} // namespace

void setup() {
    if (!driver::safeBoot()) return;
    pinMode(board::pair, INPUT_PULLUP);
    pinMode(board::config, INPUT_PULLUP);
    pinMode(board::led, OUTPUT);

    Serial.begin(115200);
    Serial.printf("HRM SmartFan v2 %s AC=%d reset=%d\n", FAN_BUILD_ID, FAN_AC_HARDWARE, int(esp_reset_reason()));

    app::radioRequest = xQueueCreate(1, sizeof(app::Request));
    app::radioView = xQueueCreate(1, sizeof(app::RadioView));
    app::controlView = xQueueCreate(1, sizeof(Snapshot));
    app::configInput = xQueueCreate(1, sizeof(app::ConfigView));
    app::activeConfigView = xQueueCreate(1, sizeof(app::ConfigView));
    app::configSaveRequest = xQueueCreate(1, sizeof(Config));
    app::uiCommand = xQueueCreate(1, sizeof(app::UiCommand));
    app::hrInput = xQueueCreate(16, sizeof(app::Sample));
    if (!app::radioRequest || !app::radioView || !app::controlView || !app::configInput ||
        !app::activeConfigView || !app::configSaveRequest || !app::uiCommand || !app::hrInput) {
        Serial.println("Blad inicjalizacji kolejek");
        return;
    }
    app::uiReady.store(app::displayBegin());
    if (!app::uiReady.load()) Serial.println("Blad inicjalizacji ekranu");
    app::webBegin();

    if (xTaskCreatePinnedToCore(app::controlTask, "Sterowanie", 4096, nullptr, 4, &controlHandle, 1) != pdPASS) {
        Serial.println("Blad zadania sterowania");
        return;
    }
    if (xTaskCreatePinnedToCore(app::radioTask, "BLE_NVS", 8192, nullptr, 2, &radioHandle, 0) != pdPASS) {
        Serial.println("Blad zadania BLE");
    }
}

void loop() {
    app::webUpdate();
    static uint32_t displayAt = 0;
    if (app::uiReady.load() && elapsed(millis(), displayAt) >= 150) {
        displayAt = millis();
        app::displayUpdate();
    }
    static uint32_t diagnosedAt = 0;
    if (elapsed(millis(), diagnosedAt) >= 5000) {
        diagnosedAt = millis();
        printDiagnostics();
    }
    delay(20);
}
