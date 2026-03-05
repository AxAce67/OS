#include "syscall/syscall.hpp"

#include "console.hpp"
#include "timer.hpp"

extern Console* console;

namespace syscall {
namespace {

constexpr uint64_t kAbiVersion = 1;
constexpr uint64_t kMaxWriteLen = 512;

int64_t HandleWriteText(uint64_t arg0, uint64_t arg1) {
    const char* text = reinterpret_cast<const char*>(arg0);
    if (text == nullptr) {
        return kErrFault;
    }

    uint64_t max_len = arg1;
    if (max_len == 0 || max_len > kMaxWriteLen) {
        max_len = kMaxWriteLen;
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

int64_t Dispatch(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t, uint64_t) {
    switch (static_cast<Number>(number)) {
        case Number::kWriteText:
            return HandleWriteText(arg0, arg1);
        case Number::kCurrentTick:
            return static_cast<int64_t>(CurrentTick());
        case Number::kAbiVersion:
            return static_cast<int64_t>(kAbiVersion);
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
