#pragma once

#include <stdint.h>

enum class Ring3ReturnReason : uint8_t {
    kNone = 0,
    kExit = 1,
    kYield = 2,
};

int64_t InvokeSyscallInt80(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int RunUserModeFunction(uint64_t entry_rip, uint64_t user_rsp);
int RunUserModeFunctionWithArgs(uint64_t entry_rip, uint64_t user_rsp, uint64_t arg0, uint64_t arg1, uint64_t arg2);
int64_t GetLastRing3ExitCode();
Ring3ReturnReason GetLastRing3ReturnReason();
