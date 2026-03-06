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

struct Snapshot {
    bool autosched_enabled;
    uint64_t last_autosched_tick;
    int tick_burst_remaining;
    uint32_t last_run_pid;
};

const char* PolicyName();
bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
Snapshot GetSnapshot();
bool RunProcessWithResult(uint32_t pid, proc::BootFileLookup lookup, RunResult* out_result);
bool RunPid(proc::BootFileLookup lookup, uint32_t pid, RunResult* out_result);
bool RunNextRunnableProcess(proc::BootFileLookup lookup, RunResult* out_result);
bool RunNextReadyProcess(proc::BootFileLookup lookup, RunResult* out_result);
bool AdvanceProcessForWait(uint32_t pid, RunResult* out_result);
int RunAllReadyProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results);
int RunAllYieldedProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results);
bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info);
int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         RunResult* out_results,
                         int max_results);

}  // namespace scheduler
