#pragma once
#include <stdint.h>

void InitializeLAPICTimer();
void NotifyTimerTick();
uint64_t CurrentTick();

