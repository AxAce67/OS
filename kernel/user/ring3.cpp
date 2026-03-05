#include "user/ring3.hpp"

#include "memory.hpp"

namespace usermode {
namespace {

Ring3PrepState g_state = {};

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

}  // namespace usermode
