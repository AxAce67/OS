#pragma once

#include <stdint.h>

namespace usermode {

struct Ring3PrepState {
    bool ready;
    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t stack_pages;
};

bool PrepareRing3Stack(uint64_t pages = 1);
void ResetRing3Stack();
Ring3PrepState GetRing3PrepState();

}  // namespace usermode
