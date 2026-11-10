#include <Arduino.h>

#include "src/App.h"
#include "src/Board.h"
#include "src/Driver.h"

#ifndef FAN_BUILD_ID
#define FAN_BUILD_ID "lokalny"
#endif

using namespace fan;

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
    app::hrInput = xQueueCreate(16, sizeof(app::Sample));
    if (!app::radioRequest || !app::radioView || !app::controlView || !app::configInput || !app::hrInput) {
        Serial.println("Blad inicjalizacji kolejek");
        return;
    }
    app::uiReady.store(app::displayBegin());
    if (!app::uiReady.load()) Serial.println("Blad inicjalizacji ekranu");

    if (xTaskCreatePinnedToCore(app::controlTask, "Sterowanie", 4096, nullptr, 4, nullptr, 1) != pdPASS) {
        Serial.println("Blad zadania sterowania");
        return;
    }
    if (xTaskCreatePinnedToCore(app::radioTask, "BLE_NVS", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
        Serial.println("Blad zadania BLE");
    }
}

void loop() {
    if (app::uiReady.load()) app::displayUpdate();
    delay(150);
}
