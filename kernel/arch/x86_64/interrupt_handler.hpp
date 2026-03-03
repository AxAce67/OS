#pragma once
#include <stdint.h>

void InitializeInterruptHandlers();
void EnqueueAbsolutePointerEvent(int32_t x, int32_t y, int32_t wheel = 0);

extern volatile uint64_t g_keyboard_dropped_events;
extern volatile uint64_t g_mouse_dropped_events;
