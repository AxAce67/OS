#include "proc/scheduler.hpp"

namespace scheduler {
namespace {

bool g_auto_schedule_enabled = false;

}  // namespace

bool IsAutoScheduleEnabled() {
    return g_auto_schedule_enabled;
}

void SetAutoScheduleEnabled(bool enabled) {
    g_auto_schedule_enabled = enabled;
}

bool GetNextAutoScheduledProcess(proc::Info* out_info) {
    if (!g_auto_schedule_enabled) {
        return false;
    }
    return proc::FindNextReadyProcess(out_info);
}

}  // namespace scheduler
