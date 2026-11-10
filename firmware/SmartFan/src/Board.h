#pragma once

#include "Core.h"

#ifndef FAN_AC_HARDWARE
#define FAN_AC_HARDWARE 0
#endif

namespace fan::board {

constexpr int sck = 10, mosi = 11, cs = 9, dc = 8, reset = 14, backlight = 2;
constexpr int zeroCross = 4, gate = 18, led = 13, pair = 12, config = 16;

#if FAN_AC_HARDWARE
// Uzupełnić dopiero na stanowisku. Zatwierdzenie wymaga wszystkich pól.
constexpr DriveProfile drive{};
#else
constexpr DriveProfile drive{true, 30, 95, 0, 0, 300, 8100, 100, false};
#endif

} // namespace fan::board
