#pragma once

#include "Core.h"

#ifndef FAN_AC_HARDWARE
#define FAN_AC_HARDWARE 1
#endif

namespace fan::board {

constexpr int sck = 10, mosi = 11, cs = 9, dc = 8, reset = 14, backlight = 2;
constexpr int zeroCross = 4, gate = 18, led = 13, pair = 12, config = 16;

// Profil fazowy 50 Hz dla modułu RobotDyn i silnika indukcyjnego (impuls 250 us, okno 800..7800 us).
constexpr DriveProfile drive{true, 30, 95, 50, 400, 800, 7800, 250, false};

} // namespace fan::board
