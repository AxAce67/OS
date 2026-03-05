#include "user/ring3.hpp"

#include "arch/x86_64/syscall_entry.hpp"
#include "memory.hpp"
#include "syscall/syscall.hpp"

namespace usermode {
namespace {

Ring3PrepState g_state = {};

extern "C" __attribute__((noinline)) void Ring3HelloEntry() {
    static const char kMessage[] = "[ring3] hello via int80\n";

    uint64_t ret = static_cast<uint64_t>(syscall::Number::kWriteText);
    __asm__ volatile(
        "int $0x80"
        : "+a"(ret)
        : "D"(reinterpret_cast<uint64_t>(kMessage)),
          "S"(static_cast<uint64_t>(sizeof(kMessage) - 1)),
          "d"(0ULL),
          "c"(0ULL)
        : "r8", "r9", "r10", "r11", "memory");

    ret = static_cast<uint64_t>(syscall::Number::kExitToKernel);
    __asm__ volatile(
        "int $0x80"
        : "+a"(ret)
        : "D"(0ULL), "S"(0ULL), "d"(0ULL), "c"(0ULL)
        : "r8", "r9", "r10", "r11", "memory");

    for (;;) {
        __asm__ volatile("pause");
    }
}

}  // namespace

bool PrepareRing3Stack(uint64_t pages) {
    if (pages == 0) {
        return false;
    }
    if (g_state.ready) {
        return true;
    }
    if (memory_manager == nullptr) {
        return false;
    }

    uint64_t base = memory_manager->Allocate(static_cast<size_t>(pages));
    if (base == 0) {
        return false;
    }

    g_state.ready = true;
    g_state.stack_base = base;
    g_state.stack_top = base + pages * kPageSize;
    g_state.stack_pages = pages;
    return true;
}

void ResetRing3Stack() {
    if (!g_state.ready || memory_manager == nullptr) {
        g_state = {};
        return;
    }
    memory_manager->Free(g_state.stack_base, static_cast<size_t>(g_state.stack_pages));
    g_state = {};
}

Ring3PrepState GetRing3PrepState() {
    return g_state;
}

bool RunRing3Hello() {
    if (!g_state.ready) {
        if (!PrepareRing3Stack(1)) {
            return false;
        }
    }
    uint64_t user_rsp = g_state.stack_top - 16;
    RunUserModeFunction(reinterpret_cast<uint64_t>(&Ring3HelloEntry), user_rsp);
    return true;
}

}  // namespace usermode
