#pragma once

#include "Board.h"

namespace fan::driver {

bool safeBoot();
bool begin();
Drive status();
void publish(uint8_t demand);

} // namespace fan::driver
