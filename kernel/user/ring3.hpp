#pragma once

#include <stdint.h>

namespace usermode {

struct Ring3PrepState {
    bool ready;
    uint64_t code_base;
    uint64_t code_top;
    uint64_t code_pages;
    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t stack_pages;
};

bool PrepareRing3Stack(uint64_t pages = 1);
void ResetRing3Stack();
void GetRing3PrepState(Ring3PrepState* out_state);
bool RunRing3Hello();
bool RunRing3BadPtrTest();
int64_t GetLastRing3SyscallReturn();

}  // namespace usermode
