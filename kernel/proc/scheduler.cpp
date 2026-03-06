#include "proc/scheduler.hpp"
#include "shell/text.hpp"

namespace scheduler {
namespace {

bool g_auto_schedule_enabled = false;
uint64_t g_last_autosched_tick = 0;
uint64_t g_autosched_tick_count = 0;
uint64_t g_autosched_run_count = 0;
uint64_t g_autosched_yield_count = 0;
int g_tick_burst_remaining = 0;
bool g_autosched_rearm_pending = false;
uint32_t g_last_run_pid = 0;
proc::State g_last_run_state = proc::State::kFree;
int64_t g_last_wait_status = 0;
constexpr int kAutoScheduleBurstLimit = 4;

void InitializeRunResult(RunResult* result, const proc::Info& info) {
    if (result == nullptr) {
        return;
    }
    result->queued_info.used = info.used;
    result->queued_info.pid = info.pid;
    result->queued_info.state = info.state;
    result->queued_info.argc = info.argc;
    result->queued_info.yield_count = info.yield_count;
    result->queued_info.resume_count = info.resume_count;
    result->queued_info.exit_code = info.exit_code;
    result->queued_info.start_tick = info.start_tick;
    result->queued_info.end_tick = info.end_tick;
    CopyString(result->queued_info.path, info.path, sizeof(result->queued_info.path));
    result->final_info.used = false;
    result->final_info.pid = info.pid;
    result->final_info.state = proc::State::kFree;
    result->final_info.argc = 0;
    result->final_info.yield_count = 0;
    result->final_info.resume_count = 0;
    result->final_info.exit_code = 0;
    result->final_info.start_tick = 0;
    result->final_info.end_tick = 0;
    result->final_info.path[0] = '\0';
    result->wait_status = 0;
    result->ok = false;
}

void FinalizeRunResult(RunResult* result, const proc::Info& fallback_info) {
    if (result == nullptr) {
        return;
    }
    if (proc::GetProcessInfo(fallback_info.pid, &result->final_info)) {
        return;
    }
    result->final_info.used = fallback_info.used;
    result->final_info.pid = fallback_info.pid;
    result->final_info.state = fallback_info.state;
    result->final_info.argc = fallback_info.argc;
    result->final_info.yield_count = fallback_info.yield_count;
    result->final_info.resume_count = fallback_info.resume_count;
    result->final_info.exit_code = fallback_info.exit_code;
    result->final_info.start_tick = fallback_info.start_tick;
    result->final_info.end_tick = fallback_info.end_tick;
    CopyString(result->final_info.path, fallback_info.path, sizeof(result->final_info.path));
}

bool RunProcessAndCollectResult(const proc::Info& info,
                                proc::BootFileLookup lookup,
                                RunResult* out_result) {
    if (out_result == nullptr) {
        return false;
    }
    InitializeRunResult(out_result, info);
    g_last_run_pid = info.pid;
    out_result->ok = proc::RunProcessByPid(info.pid, lookup, &out_result->wait_status);
    FinalizeRunResult(out_result, out_result->queued_info);
    g_last_run_state = out_result->final_info.state;
    g_last_wait_status = out_result->wait_status;
    return out_result->ok;
}

}  // namespace

const char* PolicyName() {
    return "ready=round-robin,yielded=round-robin";
}

bool IsAutoScheduleEnabled() {
    return g_auto_schedule_enabled;
}

void SetAutoScheduleEnabled(bool enabled) {
    const bool was_enabled = g_auto_schedule_enabled;
    g_auto_schedule_enabled = enabled;
    if (!enabled) {
        g_tick_burst_remaining = 0;
        g_autosched_rearm_pending = false;
    } else if (!was_enabled) {
        g_autosched_rearm_pending = true;
    }
}

Snapshot GetSnapshot() {
    Snapshot snapshot{};
    snapshot.autosched_enabled = g_auto_schedule_enabled;
    snapshot.last_autosched_tick = g_last_autosched_tick;
    snapshot.autosched_tick_count = g_autosched_tick_count;
    snapshot.autosched_run_count = g_autosched_run_count;
    snapshot.autosched_yield_count = g_autosched_yield_count;
    snapshot.tick_burst_remaining = g_tick_burst_remaining;
    snapshot.last_run_pid = g_last_run_pid;
    snapshot.last_run_state = g_last_run_state;
    snapshot.last_wait_status = g_last_wait_status;
    return snapshot;
}

void ResetSnapshot() {
    g_last_autosched_tick = 0;
    g_autosched_tick_count = 0;
    g_autosched_run_count = 0;
    g_autosched_yield_count = 0;
    g_tick_burst_remaining = 0;
    g_autosched_rearm_pending = g_auto_schedule_enabled;
    g_last_run_pid = 0;
    g_last_run_state = proc::State::kFree;
    g_last_wait_status = 0;
}

bool RunProcessWithResult(uint32_t pid, proc::BootFileLookup lookup, RunResult* out_result) {
    proc::Info info{};
    if (!proc::GetProcessInfo(pid, &info)) {
        return false;
    }
    return RunProcessAndCollectResult(info, lookup, out_result);
}

bool RunPid(proc::BootFileLookup lookup, uint32_t pid, RunResult* out_result) {
    if (lookup == nullptr || out_result == nullptr) {
        return false;
    }
    proc::Info info{};
    if (!proc::GetProcessInfo(pid, &info)) {
        return false;
    }
    if (info.state != proc::State::kReady && info.state != proc::State::kYielded) {
        return false;
    }
    return RunProcessAndCollectResult(info, lookup, out_result);
}

bool RunNextRunnableProcess(proc::BootFileLookup lookup, RunResult* out_result) {
    if (lookup == nullptr || out_result == nullptr) {
        return false;
    }
    proc::Info info{};
    if (!proc::PeekNextRunnableProcess(&info)) {
        return false;
    }
    proc::AdvanceRunnableProcessCursor();
    return RunProcessAndCollectResult(info, lookup, out_result);
}

bool RunNextReadyProcess(proc::BootFileLookup lookup, RunResult* out_result) {
    if (lookup == nullptr || out_result == nullptr) {
        return false;
    }
    proc::Info info{};
    if (!proc::PeekNextRunnableProcess(&info)) {
        return false;
    }
    if (info.state != proc::State::kReady) {
        return false;
    }
    proc::AdvanceRunnableProcessCursor();
    return RunProcessAndCollectResult(info, lookup, out_result);
}

bool AdvanceProcessForWait(uint32_t pid, RunResult* out_result) {
    proc::Info info{};
    if (!proc::GetProcessInfo(pid, &info)) {
        return false;
    }
    if (info.state != proc::State::kYielded) {
        return false;
    }
    return RunProcessAndCollectResult(info, nullptr, out_result);
}

int RunAllReadyProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results) {
    if (lookup == nullptr || out_results == nullptr || max_results <= 0) {
        return 0;
    }
    int ran = 0;
    while (ran < max_results) {
        if (!RunNextReadyProcess(lookup, &out_results[ran])) {
            break;
        }
        ++ran;
    }
    return ran;
}

int RunAllYieldedProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results) {
    if (lookup == nullptr || out_results == nullptr || max_results <= 0) {
        return 0;
    }
    int ran = 0;
    while (ran < max_results) {
        proc::Info info{};
        if (!proc::PeekNextYieldedProcess(&info)) {
            break;
        }
        proc::AdvanceYieldedProcessCursor();
        RunProcessAndCollectResult(info, lookup, &out_results[ran]);
        ++ran;
        if (out_results[ran - 1].final_info.state == proc::State::kYielded) {
            break;
        }
    }
    return ran;
}

bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info) {
    if (!g_auto_schedule_enabled) {
        return false;
    }
    if (g_autosched_rearm_pending || now_tick != g_last_autosched_tick) {
        g_last_autosched_tick = now_tick;
        ++g_autosched_tick_count;
        g_tick_burst_remaining = kAutoScheduleBurstLimit;
        g_autosched_rearm_pending = false;
    }
    if (g_tick_burst_remaining <= 0) {
        return false;
    }
    if (!proc::PeekNextRunnableProcess(out_info)) {
        return false;
    }
    proc::AdvanceRunnableProcessCursor();
    --g_tick_burst_remaining;
    return true;
}

int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         RunResult* out_results,
                         int max_results) {
    if (lookup == nullptr || out_results == nullptr || max_results <= 0) {
        return 0;
    }
    int ran = 0;
    while (ran < max_results) {
        proc::Info info{};
        if (!DequeueAutoScheduledProcessForTick(now_tick, &info)) {
            break;
        }
        RunProcessAndCollectResult(info, lookup, &out_results[ran]);
        ++g_autosched_run_count;
        ++ran;
        if (out_results[ran - 1].final_info.state == proc::State::kYielded) {
            ++g_autosched_yield_count;
            break;
        }
    }
    return ran;
}

}  // namespace scheduler
