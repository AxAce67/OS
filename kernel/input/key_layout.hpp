#pragma once

#include <stdint.h>

char KeycodeToAsciiByLayout(uint8_t keycode, bool shift, bool caps_lock, bool num_lock, bool jp_layout);
