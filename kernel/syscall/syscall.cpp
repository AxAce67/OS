#include "syscall/syscall.hpp"

#include "console.hpp"
#include "timer.hpp"
#include "user/ring3.hpp"

extern Console* console;

namespace syscall {
namespace {

constexpr uint64_t kAbiVersion = 1;
constexpr uint64_t kMaxWriteLen = 512;
constexpr uint64_t kMaxEnvKeyLen = 64;
constexpr uint64_t kMaxEnvValueLen = 96;
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

bool ValidateWriteRange(uint64_t address, uint64_t length, bool from_user) {
    return ValidateReadRange(address, length, from_user);
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

uint64_t FindChar(const char* s, char ch) {
    if (s == nullptr) {
        return static_cast<uint64_t>(-1);
    }
    for (uint64_t i = 0;; ++i) {
        if (s[i] == '\0') {
            return static_cast<uint64_t>(-1);
        }
        if (s[i] == ch) {
            return i;
        }
    }
}

uint64_t StrLen(const char* s) {
    if (s == nullptr) {
        return 0;
    }
    uint64_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

bool BytesEqual(const char* a, const char* b, uint64_t n) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    for (uint64_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

int64_t HandleGetEnv(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, bool from_user) {
    if (arg0 == 0) {
        return kErrFault;
    }
    const uint64_t key_len = arg1;
    if (key_len == 0 || key_len > kMaxEnvKeyLen) {
        return kErrInvalid;
    }
    if (!ValidateReadRange(arg0, key_len, from_user)) {
        return kErrFault;
    }
    if (arg2 == 0 && arg3 != 0) {
        return kErrFault;
    }
    if (arg2 != 0 && arg3 != 0 && !ValidateWriteRange(arg2, arg3, from_user)) {
        return kErrFault;
    }

    char key[kMaxEnvKeyLen + 1];
    const char* src_key = reinterpret_cast<const char*>(arg0);
    for (uint64_t i = 0; i < key_len; ++i) {
        key[i] = src_key[i];
    }
    key[key_len] = '\0';

    const char* const* envp = usermode::GetCurrentExecEnvp();
    const int envc = usermode::GetCurrentExecEnvc();
    if (envp == nullptr || envc <= 0) {
        return kErrInvalid;
    }

    for (int i = 0; i < envc; ++i) {
        const char* entry = envp[i];
        const uint64_t eq = FindChar(entry, '=');
        if (eq == static_cast<uint64_t>(-1)) {
            continue;
        }
        if (eq != key_len) {
            continue;
        }
        if (!BytesEqual(entry, key, key_len)) {
            continue;
        }
        const char* value = entry + eq + 1;
        const uint64_t value_len = StrLen(value);
        if (arg2 == 0 || arg3 == 0) {
            return static_cast<int64_t>(value_len);
        }
        if (arg3 <= value_len) {
            return kErrInvalid;
        }
        char* out = reinterpret_cast<char*>(arg2);
        for (uint64_t n = 0; n < value_len; ++n) {
            out[n] = value[n];
        }
        out[value_len] = '\0';
        return static_cast<int64_t>(value_len);
    }

    return kErrInvalid;
}

int64_t HandleSetEnv(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, bool from_user) {
    if (arg0 == 0) {
        return kErrFault;
    }
    const uint64_t key_len = arg1;
    const uint64_t value_len = arg3;
    if (key_len == 0 || key_len > kMaxEnvKeyLen || value_len > kMaxEnvValueLen) {
        return kErrInvalid;
    }
    if (!ValidateReadRange(arg0, key_len, from_user)) {
        return kErrFault;
    }
    if (value_len > 0) {
        if (arg2 == 0 || !ValidateReadRange(arg2, value_len, from_user)) {
            return kErrFault;
        }
    }

    char key[kMaxEnvKeyLen + 1];
    const char* src_key = reinterpret_cast<const char*>(arg0);
    for (uint64_t i = 0; i < key_len; ++i) {
        if (src_key[i] == '=' || src_key[i] == '\0') {
            return kErrInvalid;
        }
        key[i] = src_key[i];
    }
    key[key_len] = '\0';

    char value[kMaxEnvValueLen + 1];
    if (value_len > 0) {
        const char* src_value = reinterpret_cast<const char*>(arg2);
        for (uint64_t i = 0; i < value_len; ++i) {
            if (src_value[i] == '\0') {
                return kErrInvalid;
            }
            value[i] = src_value[i];
        }
    }
    value[value_len] = '\0';

    return usermode::SetCurrentExecEnv(key, value) ? 0 : kErrInvalid;
}

int64_t HandleUnsetEnv(uint64_t arg0, uint64_t arg1, bool from_user) {
    if (arg0 == 0) {
        return kErrFault;
    }
    const uint64_t key_len = arg1;
    if (key_len == 0 || key_len > kMaxEnvKeyLen) {
        return kErrInvalid;
    }
    if (!ValidateReadRange(arg0, key_len, from_user)) {
        return kErrFault;
    }

    char key[kMaxEnvKeyLen + 1];
    const char* src_key = reinterpret_cast<const char*>(arg0);
    for (uint64_t i = 0; i < key_len; ++i) {
        if (src_key[i] == '=' || src_key[i] == '\0') {
            return kErrInvalid;
        }
        key[i] = src_key[i];
    }
    key[key_len] = '\0';
    return usermode::UnsetCurrentExecEnv(key) ? 0 : kErrInvalid;
}

}  // namespace

int64_t Dispatch(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return DispatchFromTrap(number, arg0, arg1, arg2, arg3, false);
}

int64_t DispatchFromTrap(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, bool from_user) {
    switch (static_cast<Number>(number)) {
        case Number::kWriteText:
            return HandleWriteText(arg0, arg1, from_user);
        case Number::kCurrentTick:
            return static_cast<int64_t>(CurrentTick());
        case Number::kAbiVersion:
            return static_cast<int64_t>(kAbiVersion);
        case Number::kExitToKernel:
            return 0;
        case Number::kGetEnv:
            return HandleGetEnv(arg0, arg1, arg2, arg3, from_user);
        case Number::kSetEnv:
            return HandleSetEnv(arg0, arg1, arg2, arg3, from_user);
        case Number::kUnsetEnv:
            return HandleUnsetEnv(arg0, arg1, from_user);
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
