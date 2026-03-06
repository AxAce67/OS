#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

struct TickRunResult {
    proc::Info info;
    int64_t wait_status;
    bool ok;
};

bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info);
int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         TickRunResult* out_results,
                         int max_results);

}  // namespace scheduler
