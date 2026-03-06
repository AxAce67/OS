#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

struct RunResult {
    proc::Info queued_info;
    proc::Info final_info;
    int64_t wait_status;
    bool ok;
};

const char* PolicyName();
bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
bool RunProcessWithResult(uint32_t pid, proc::BootFileLookup lookup, RunResult* out_result);
bool AdvanceProcessForWait(uint32_t pid, RunResult* out_result);
int RunAllReadyProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results);
bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info);
int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         RunResult* out_results,
                         int max_results);

}  // namespace scheduler
