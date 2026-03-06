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

bool RunAutoScheduledProcess(proc::BootFileLookup lookup, proc::Info* out_info, int64_t* out_wait_status) {
    if (!g_auto_schedule_enabled) {
        return false;
    }
    return proc::RunNextReadyProcess(lookup, out_info, out_wait_status);
}

}  // namespace scheduler
