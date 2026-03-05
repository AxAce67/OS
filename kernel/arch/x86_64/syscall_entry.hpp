#pragma once

#include <stdint.h>

int64_t InvokeSyscallInt80(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int RunUserModeFunction(uint64_t entry_rip, uint64_t user_rsp);
int64_t GetLastRing3ExitCode();
