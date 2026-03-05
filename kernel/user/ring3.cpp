#include "user/ring3.hpp"

#include "arch/x86_64/paging.hpp"
#include "arch/x86_64/syscall_entry.hpp"
#include "memory.hpp"
#include "syscall/syscall.hpp"

namespace usermode {
namespace {

Ring3PrepState g_state = {};
volatile int64_t g_last_ring3_syscall_ret = 0;
const char* g_last_ring3_error = "ok";

constexpr uint64_t kCodePageCount = 1;
constexpr uint64_t kDefaultStackPages = 1;
constexpr uint64_t kMaxStackPages = 16;
constexpr uint64_t kMaxUserImageBytes = 128 * 1024;

struct Ring3UserBinHeader {
    uint8_t magic[8];
    uint32_t version;
    uint32_t entry_offset;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t stack_pages;
} __attribute__((packed));

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

uint32_t BuildUserProgram(bool fault_case, uint64_t message_addr, uint64_t message_len,
                          uint64_t ret_slot_addr, uint8_t* code, uint32_t capacity) {
    uint32_t o = 0;
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xB8);
    Emit64(code, capacity, &o, static_cast<uint64_t>(syscall::Number::kWriteText));
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xBF);
    Emit64(code, capacity, &o, fault_case ? 0x8ULL : message_addr);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xBE);
    Emit64(code, capacity, &o, fault_case ? 32ULL : message_len);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xD2);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xC9);
    Emit8(code, capacity, &o, 0xCD); Emit8(code, capacity, &o, 0x80);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xA3);
    Emit64(code, capacity, &o, ret_slot_addr);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0xB8);
    Emit64(code, capacity, &o, static_cast<uint64_t>(syscall::Number::kExitToKernel));
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xFF);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xF6);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xD2);
    Emit8(code, capacity, &o, 0x48); Emit8(code, capacity, &o, 0x31); Emit8(code, capacity, &o, 0xC9);
    Emit8(code, capacity, &o, 0xCD); Emit8(code, capacity, &o, 0x80);
    Emit8(code, capacity, &o, 0xEB); Emit8(code, capacity, &o, 0xFE);
    return o;
}

uint64_t AlignUp(uint64_t value, uint64_t align) {
    if (align == 0) {
        return value;
    }
    const uint64_t rem = value % align;
    return rem == 0 ? value : (value + (align - rem));
}

void CopyBytes(uint8_t* dst, const uint8_t* src, uint64_t n) {
    if (dst == nullptr || src == nullptr) {
        return;
    }
    for (uint64_t i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
}

bool AllocateRing3Memory(uint64_t code_pages, uint64_t stack_pages) {
    if (code_pages == 0 || stack_pages == 0 || stack_pages > kMaxStackPages || memory_manager == nullptr) {
        g_last_ring3_error = "alloc.args";
        return false;
    }

    if (g_state.ready) {
        if (g_state.code_pages == code_pages && g_state.stack_pages == stack_pages) {
            return true;
        }
        ResetRing3Stack();
    }

    const uint64_t code_base = memory_manager->Allocate(static_cast<size_t>(code_pages));
    if (code_base == 0) {
        g_last_ring3_error = "alloc.code";
        return false;
    }
    const uint64_t stack_base = memory_manager->Allocate(static_cast<size_t>(stack_pages));
    if (stack_base == 0) {
        memory_manager->Free(code_base, static_cast<size_t>(code_pages));
        g_last_ring3_error = "alloc.stack";
        return false;
    }

    if (!MapIdentityRangeUser(code_base, code_pages * kPageSize) ||
        !MapIdentityRangeUser(stack_base, stack_pages * kPageSize)) {
        UnmapIdentityRangeUser(code_base, code_pages * kPageSize);
        memory_manager->Free(code_base, static_cast<size_t>(code_pages));
        memory_manager->Free(stack_base, static_cast<size_t>(stack_pages));
        g_last_ring3_error = "map.user";
        return false;
    }

    g_state.ready = true;
    g_state.code_base = code_base;
    g_state.code_top = code_base + code_pages * kPageSize;
    g_state.code_pages = code_pages;
    g_state.stack_base = stack_base;
    g_state.stack_top = stack_base + stack_pages * kPageSize;
    g_state.stack_pages = stack_pages;
    return true;
}

bool BuildAndRunUserProgram(bool fault_case) {
    if (!AllocateRing3Memory(kCodePageCount, kDefaultStackPages)) {
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
    g_last_ring3_error = "ok";
    return true;
}

bool ValidateUserBinHeader(const Ring3UserBinHeader* h, uint64_t file_size) {
    if (h == nullptr || file_size < sizeof(Ring3UserBinHeader)) {
        g_last_ring3_error = "bin.header";
        return false;
    }
    const uint8_t expected[8] = {'R', '3', 'B', 'I', 'N', '0', '1', '\0'};
    for (int i = 0; i < 8; ++i) {
        if (h->magic[i] != expected[i]) {
            g_last_ring3_error = "bin.magic";
            return false;
        }
    }
    if (h->version != 1) {
        g_last_ring3_error = "bin.version";
        return false;
    }
    if (h->image_size == 0 || h->image_size > kMaxUserImageBytes) {
        g_last_ring3_error = "bin.size";
        return false;
    }
    if (h->entry_offset >= h->image_size) {
        g_last_ring3_error = "bin.entry";
        return false;
    }
    const uint64_t image_start = h->image_offset;
    const uint64_t image_end = image_start + h->image_size;
    if (image_end < image_start || image_end > file_size) {
        g_last_ring3_error = "bin.range";
        return false;
    }
    const uint64_t stack_pages = (h->stack_pages == 0) ? kDefaultStackPages : h->stack_pages;
    if (stack_pages > kMaxStackPages) {
        g_last_ring3_error = "bin.stack";
        return false;
    }
    return true;
}

}  // namespace

bool PrepareRing3Stack(uint64_t pages) {
    if (pages == 0) {
        pages = kDefaultStackPages;
    }
    const bool ok = AllocateRing3Memory(kCodePageCount, pages);
    if (ok) {
        g_last_ring3_error = "ok";
    }
    return ok;
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
    return BuildAndRunUserProgram(false);
}

bool RunRing3BadPtrTest() {
    return BuildAndRunUserProgram(true);
}

int64_t GetLastRing3SyscallReturn() {
    return g_last_ring3_syscall_ret;
}

bool RunRing3BinaryFromBuffer(const uint8_t* data, uint64_t size) {
    if (data == nullptr) {
        g_last_ring3_error = "bin.null";
        return false;
    }
    const Ring3UserBinHeader* header = reinterpret_cast<const Ring3UserBinHeader*>(data);
    if (!ValidateUserBinHeader(header, size)) {
        return false;
    }

    const uint64_t stack_pages = (header->stack_pages == 0) ? kDefaultStackPages : header->stack_pages;
    const uint64_t image_pages = AlignUp(header->image_size, kPageSize) / kPageSize;
    if (!AllocateRing3Memory(image_pages, stack_pages)) {
        return false;
    }

    uint8_t* dst = reinterpret_cast<uint8_t*>(g_state.code_base);
    for (uint64_t i = 0; i < g_state.code_pages * kPageSize; ++i) {
        dst[i] = 0;
    }
    CopyBytes(dst, data + header->image_offset, header->image_size);

    const uint64_t entry = g_state.code_base + header->entry_offset;
    const uint64_t user_rsp = g_state.stack_top - 16;
    RunUserModeFunction(entry, user_rsp);
    g_last_ring3_error = "ok";
    return true;
}

const char* GetLastRing3Error() {
    return g_last_ring3_error;
}

}  // namespace usermode
