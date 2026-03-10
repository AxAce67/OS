#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

enum class WaitStepResult : uint8_t {
    kInvalid = 0,
    kPending,
    kAdvanced,
    kComplete,
};

struct RunResult {
    proc::Info queued_info;
    proc::Info final_info;
    int64_t wait_status;
    bool ok;
};

struct Snapshot {
    bool autosched_enabled;
    uint64_t last_autosched_tick;
    uint64_t autosched_tick_count;
    uint64_t autosched_run_count;
    uint64_t autosched_yield_count;
    int tick_burst_remaining;
    uint32_t last_run_pid;
    proc::State last_run_state;
    int64_t last_wait_status;
    uint32_t last_yield_pid;
    uint64_t last_yield_tick;
};

const char* PolicyName();
bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
Snapshot GetSnapshot();
void ResetSnapshot();
WaitStepResult StepWaitProcess(uint32_t pid,
                               bool allow_advance,
                               int64_t* out_exit_code,
                               RunResult* out_result);
bool RunProcessWithResult(uint32_t pid, proc::BootFileLookup lookup, RunResult* out_result);
bool RunPid(proc::BootFileLookup lookup, uint32_t pid, RunResult* out_result);
bool RunNextRunnableProcess(proc::BootFileLookup lookup, RunResult* out_result);
bool RunNextReadyProcess(proc::BootFileLookup lookup, RunResult* out_result);
int RunAllReadyProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results);
int RunAllYieldedProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results);
bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info);
int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         RunResult* out_results,
                         int max_results);

}  // namespace scheduler
