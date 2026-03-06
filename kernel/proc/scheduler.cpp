#include "proc/scheduler.hpp"

namespace scheduler {
namespace {

bool g_auto_schedule_enabled = false;
uint64_t g_last_autosched_tick = 0;
int g_tick_burst_remaining = 0;
constexpr int kAutoScheduleBurstLimit = 4;

}  // namespace

bool IsAutoScheduleEnabled() {
    return g_auto_schedule_enabled;
}

void SetAutoScheduleEnabled(bool enabled) {
    g_auto_schedule_enabled = enabled;
    if (!enabled) {
        g_tick_burst_remaining = 0;
    }
}

bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info) {
    if (!g_auto_schedule_enabled) {
        return false;
    }
    if (now_tick != g_last_autosched_tick) {
        g_last_autosched_tick = now_tick;
        g_tick_burst_remaining = kAutoScheduleBurstLimit;
    }
    if (g_tick_burst_remaining <= 0) {
        return false;
    }
    if (!proc::FindNextReadyProcess(out_info)) {
        return false;
    }
    --g_tick_burst_remaining;
    return true;
}

}  // namespace scheduler
