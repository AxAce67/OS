#include "user/ring3.hpp"

#include "arch/x86_64/paging.hpp"
#include "arch/x86_64/syscall_entry.hpp"
#include "memory.hpp"
#include "syscall/syscall.hpp"

namespace usermode {
namespace {

Ring3PrepState g_state = {};
volatile int64_t g_last_ring3_syscall_ret = 0;
constexpr uint64_t kCodePageCount = 1;
constexpr uint64_t kDefaultStackPages = 1;

void Emit8(uint8_t* code, uint32_t capacity, uint32_t* pos, uint8_t v) {
    if (code == nullptr || pos == nullptr || *pos >= capacity) {
        return;
    }
    code[*pos] = v;
    ++(*pos);
}

void Emit64(uint8_t* code, uint32_t capacity, uint32_t* pos, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        Emit8(code, capacity, pos, static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

uint32_t BuildUserProgram(bool fault_case, uint64_t message_addr, uint64_t message_len, uint64_t ret_slot_addr, uint8_t* code, uint32_t capacity) {
    uint32_t o = 0;
    // mov rax, SYS_write
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xB8); Emit64(code, capacity, &o, static_cast<uint64_t>(syscall::Number::kWriteText));
    // mov rdi, msg_ptr (fault_caseでは0x8)
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xBF); Emit64(code, capacity, &o, fault_case ? 0x8ULL : message_addr);
    // mov rsi, msg_len
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xBE); Emit64(code, capacity, &o, fault_case ? 32ULL : message_len);
    // xor rdx, rdx; xor rcx, rcx
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xD2);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xC9);
    // int 0x80
    Emit8(code, capacity, &o, 0xCD); Emit8(code, capacity, &o, 0x80);
    // mov [ret_slot], rax
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xA3); Emit64(code, capacity, &o, ret_slot_addr);
    // mov rax, SYS_exit_to_kernel
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xB8); Emit64(code, capacity, &o, static_cast<uint64_t>(syscall::Number::kExitToKernel));
    // xor rdi,rdi; xor rsi,rsi; xor rdx,rdx; xor rcx,rcx
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xFF);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xF6);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xD2);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xC9);
    // int 0x80
    Emit8(code, capacity, &o, 0xCD); Emit8(code, capacity, &o, 0x80);
    // jmp $
    Emit8(code, capacity, &o, 0xEB); Emit8(code, capacity, &o, 0xFE);
    return o;
}

bool BuildAndRunUserProgram(bool fault_case) {
    if (!g_state.ready) {
        return false;
    }
    uint8_t* code = reinterpret_cast<uint8_t*>(g_state.code_base);
    const uint64_t message_addr = g_state.code_base + 0x200;
    const uint64_t ret_slot_addr = g_state.code_base + 0x180;
    const char* msg = "[ring3] hello via int80\n";
    const uint32_t msg_len = 24;

    if (!fault_case) {
        for (uint32_t i = 0; i < msg_len; ++i) {
            reinterpret_cast<uint8_t*>(message_addr)[i] = static_cast<uint8_t>(msg[i]);
        }
    }
    *reinterpret_cast<uint64_t*>(ret_slot_addr) = 0;
    BuildUserProgram(fault_case, message_addr, msg_len, ret_slot_addr, code, static_cast<uint32_t>(kPageSize));

    const uint64_t user_rsp = g_state.stack_top - 16;
    RunUserModeFunction(g_state.code_base, user_rsp);
    g_last_ring3_syscall_ret = static_cast<int64_t>(*reinterpret_cast<uint64_t*>(ret_slot_addr));
    return true;
}

}  // namespace

bool PrepareRing3Stack(uint64_t pages) {
    if (pages == 0) {
        return false;
    }
    if (g_state.ready && g_state.code_base != 0 && g_state.stack_base != 0) {
        return true;
    }
    if (memory_manager == nullptr) {
        return false;
    }
    if (pages == 0) {
        pages = kDefaultStackPages;
    }

    const uint64_t code_base = memory_manager->Allocate(static_cast<size_t>(kCodePageCount));
    if (code_base == 0) {
        return false;
    }
    const uint64_t stack_base = memory_manager->Allocate(static_cast<size_t>(pages));
    if (stack_base == 0) {
        memory_manager->Free(code_base, static_cast<size_t>(kCodePageCount));
        return false;
    }

    if (!MapIdentityRangeUser(code_base, kCodePageCount * kPageSize) ||
        !MapIdentityRangeUser(stack_base, pages * kPageSize)) {
        UnmapIdentityRangeUser(code_base, kCodePageCount * kPageSize);
        memory_manager->Free(code_base, static_cast<size_t>(kCodePageCount));
        memory_manager->Free(stack_base, static_cast<size_t>(pages));
        return false;
    }

    g_state.ready = true;
    g_state.code_base = code_base;
    g_state.code_top = code_base + kCodePageCount * kPageSize;
    g_state.code_pages = kCodePageCount;
    g_state.stack_base = stack_base;
    g_state.stack_top = stack_base + pages * kPageSize;
    g_state.stack_pages = pages;
    return true;
}

void ResetRing3Stack() {
    if (!g_state.ready || memory_manager == nullptr) {
        g_state.ready = false;
        g_state.code_base = 0;
        g_state.code_top = 0;
        g_state.code_pages = 0;
        g_state.stack_base = 0;
        g_state.stack_top = 0;
        g_state.stack_pages = 0;
        return;
    }
    UnmapIdentityRangeUser(g_state.code_base, g_state.code_pages * kPageSize);
    UnmapIdentityRangeUser(g_state.stack_base, g_state.stack_pages * kPageSize);
    memory_manager->Free(g_state.code_base, static_cast<size_t>(g_state.code_pages));
    memory_manager->Free(g_state.stack_base, static_cast<size_t>(g_state.stack_pages));
    g_state.ready = false;
    g_state.code_base = 0;
    g_state.code_top = 0;
    g_state.code_pages = 0;
    g_state.stack_base = 0;
    g_state.stack_top = 0;
    g_state.stack_pages = 0;
}

void GetRing3PrepState(Ring3PrepState* out_state) {
    if (out_state == nullptr) {
        return;
    }
    out_state->ready = g_state.ready;
    out_state->code_base = g_state.code_base;
    out_state->code_top = g_state.code_top;
    out_state->code_pages = g_state.code_pages;
    out_state->stack_base = g_state.stack_base;
    out_state->stack_top = g_state.stack_top;
    out_state->stack_pages = g_state.stack_pages;
}

bool RunRing3Hello() {
    if (!g_state.ready) {
        if (!PrepareRing3Stack(kDefaultStackPages)) {
            return false;
        }
    }
    return BuildAndRunUserProgram(false);
}

bool RunRing3BadPtrTest() {
    if (!g_state.ready) {
        if (!PrepareRing3Stack(kDefaultStackPages)) {
            return false;
        }
    }
    return BuildAndRunUserProgram(true);
}

int64_t GetLastRing3SyscallReturn() {
    return g_last_ring3_syscall_ret;
}

}  // namespace usermode
