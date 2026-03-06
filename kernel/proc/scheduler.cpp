#include "proc/scheduler.hpp"
#include "shell/text.hpp"

namespace scheduler {
namespace {

bool g_auto_schedule_enabled = false;
uint64_t g_last_autosched_tick = 0;
int g_tick_burst_remaining = 0;
constexpr int kAutoScheduleBurstLimit = 4;

void InitializeRunResult(RunResult* result, const proc::Info& info) {
    if (result == nullptr) {
        return;
    }
    result->queued_info.used = info.used;
    result->queued_info.pid = info.pid;
    result->queued_info.state = info.state;
    result->queued_info.argc = info.argc;
    result->queued_info.exit_code = info.exit_code;
    result->queued_info.start_tick = info.start_tick;
    result->queued_info.end_tick = info.end_tick;
    CopyString(result->queued_info.path, info.path, sizeof(result->queued_info.path));
    result->final_info.used = false;
    result->final_info.pid = info.pid;
    result->final_info.state = proc::State::kFree;
    result->final_info.argc = 0;
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
    out_result->ok = proc::RunProcessByPid(info.pid, lookup, &out_result->wait_status);
    FinalizeRunResult(out_result, out_result->queued_info);
    return out_result->ok;
}

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
        ++ran;
        if (out_results[ran - 1].final_info.state == proc::State::kYielded) {
            break;
        }
    }
    return ran;
}

}  // namespace scheduler
