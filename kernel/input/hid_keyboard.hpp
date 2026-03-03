#pragma once

#include <stdint.h>

// Decode USB HID boot-keyboard reports and emit Set-1 compatible scancode bytes.
// Returns true when the report looks like keyboard input (even with no key changes).
bool DecodeHIDBootKeyboardToSet1(const uint8_t* data,
                                 uint32_t len,
                                 uint8_t* out_scancodes,
                                 uint8_t* out_count,
                                 uint8_t max_out);

