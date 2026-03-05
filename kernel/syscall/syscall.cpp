#include "syscall/syscall.hpp"

#include "console.hpp"
#include "timer.hpp"

extern Console* console;

namespace syscall {
namespace {

constexpr uint64_t kAbiVersion = 1;
constexpr uint64_t kMaxWriteLen = 512;
constexpr uint64_t kMinUserAddress = 0x1000ULL;
constexpr uint64_t kUserUpperExclusive = 0x0000800000000000ULL;

bool IsCanonicalAddress(uint64_t address) {
    const uint64_t upper = address >> 48;
    return upper == 0 || upper == 0xFFFF;
}

bool ValidateReadRange(uint64_t address, uint64_t length, bool from_user) {
    if (address < kMinUserAddress) {
        return false;
    }
    if (!IsCanonicalAddress(address)) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    const uint64_t end = address + (length - 1);
    if (end < address) {
        return false;
    }
    if (!IsCanonicalAddress(end)) {
        return false;
    }
    if (from_user) {
        if (address >= kUserUpperExclusive || end >= kUserUpperExclusive) {
            return false;
        }
    }
    return true;
}

int64_t HandleWriteText(uint64_t arg0, uint64_t arg1, bool from_user) {
    const char* text = reinterpret_cast<const char*>(arg0);
    if (text == nullptr) {
        return kErrFault;
    }

    uint64_t max_len = arg1;
    if (max_len == 0 || max_len > kMaxWriteLen) {
        max_len = kMaxWriteLen;
    }
    if (!ValidateReadRange(arg0, max_len, from_user)) {
        return kErrFault;
    }

    uint64_t written = 0;
    while (written < max_len && text[written] != '\0') {
        char ch[2] = {text[written], '\0'};
        console->Print(ch);
        ++written;
    }
    return static_cast<int64_t>(written);
}

}  // namespace

int64_t Dispatch(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return DispatchFromTrap(number, arg0, arg1, arg2, arg3, false);
}

int64_t DispatchFromTrap(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t, uint64_t, bool from_user) {
    switch (static_cast<Number>(number)) {
        case Number::kWriteText:
            return HandleWriteText(arg0, arg1, from_user);
        case Number::kCurrentTick:
            return static_cast<int64_t>(CurrentTick());
        case Number::kAbiVersion:
            return static_cast<int64_t>(kAbiVersion);
        case Number::kExitToKernel:
            return 0;
        default:
            return kErrNoSys;
    }
}

const char* ErrorName(int64_t code) {
    switch (code) {
        case kErrNoSys:
            return "ENOSYS";
        case kErrFault:
            return "EFAULT";
        case kErrInvalid:
            return "EINVAL";
        default:
            return "EUNKNOWN";
    }
}

}  // namespace syscall
