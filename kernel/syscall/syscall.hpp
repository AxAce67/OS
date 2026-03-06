#pragma once

#include <stdint.h>

namespace syscall {

enum class Number : uint64_t {
    kWriteText = 1,
    kCurrentTick = 2,
    kAbiVersion = 3,
    kExitToKernel = 4,
    kGetEnv = 5,
    kSetEnv = 6,
    kUnsetEnv = 7,
    kWaitPid = 8,
};

constexpr int64_t kErrNoSys = -38;
constexpr int64_t kErrFault = -14;
constexpr int64_t kErrInvalid = -22;
constexpr int64_t kErrBusy = -16;

int64_t Dispatch(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int64_t DispatchFromTrap(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, bool from_user);
const char* ErrorName(int64_t code);

}  // namespace syscall
