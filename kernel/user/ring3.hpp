#pragma once

#include <stdint.h>
#include "arch/x86_64/syscall_entry.hpp"

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
bool RunRing3BinaryFromBuffer(const uint8_t* data, uint64_t size);
bool RunRing3BinaryFromBufferWithArgs(const uint8_t* data, uint64_t size, const char* const* argv, int argc);
bool RunRing3BinaryFromBufferWithArgsEnv(const uint8_t* data, uint64_t size,
                                         const char* const* argv, int argc,
                                         const char* const* envp, int envc);
bool RunRing3BinaryFromBufferWithContext(const uint8_t* data, uint64_t size,
                                         const char* const* argv, int argc,
                                         const char* const* envp, int envc);
const char* const* GetCurrentExecEnvp();
int GetCurrentExecEnvc();
bool SetCurrentExecEnv(const char* key, const char* value);
bool UnsetCurrentExecEnv(const char* key);
int64_t GetLastRing3SyscallReturn();
Ring3ReturnReason GetLastRing3ReturnReason();
const char* GetLastRing3Error();

}  // namespace usermode
