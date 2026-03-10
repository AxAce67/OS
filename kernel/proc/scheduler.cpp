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
uint32_t g_last_yield_pid = 0;
uint64_t g_last_yield_tick = 0;
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

bool DequeueNextRunnableInfo(proc::Info* out_info) {
    if (out_info == nullptr) {
        return false;
    }
    if (!proc::PeekNextRunnableProcess(out_info)) {
        return false;
    }
    proc::AdvanceRunnableProcessCursor();
    return true;
}

bool DequeueNextReadyInfo(proc::Info* out_info) {
    if (out_info == nullptr) {
        return false;
    }
    if (!proc::PeekNextRunnableProcess(out_info)) {
        return false;
    }
    if (out_info->state != proc::State::kReady) {
        return false;
    }
    proc::AdvanceRunnableProcessCursor();
    return true;
}

bool DequeueNextYieldedInfo(proc::Info* out_info) {
    if (out_info == nullptr) {
        return false;
    }
    if (!proc::PeekNextYieldedProcess(out_info)) {
        return false;
    }
    proc::AdvanceYieldedProcessCursor();
    return true;
}

bool DequeueAutoScheduledInfoForTick(uint64_t now_tick, proc::Info* out_info) {
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
    if (!DequeueNextRunnableInfo(out_info)) {
        return false;
    }
    --g_tick_burst_remaining;
    return true;
}

uint64_t g_pass_tick_context = 0;

bool DequeueAutoScheduledInfoFromContext(proc::Info* out_info) {
    return DequeueAutoScheduledInfoForTick(g_pass_tick_context, out_info);
}

void AccountAutoScheduledResult(uint64_t now_tick, const RunResult& result) {
    ++g_autosched_run_count;
    if (result.final_info.state != proc::State::kYielded) {
        return;
    }
    ++g_autosched_yield_count;
    g_last_yield_pid = result.final_info.pid;
    g_last_yield_tick = now_tick;
}

bool ShouldStopPassAfterResult(const RunResult& result) {
    return result.final_info.state == proc::State::kYielded;
}

enum class PassStopPolicy : uint8_t {
    kStopOnYielded = 0,
};

bool ShouldStopPassForPolicy(PassStopPolicy policy, const RunResult& result) {
    switch (policy) {
        case PassStopPolicy::kStopOnYielded:
            return ShouldStopPassAfterResult(result);
    }
    return false;
}

using SelectInfoFn = bool (*)(proc::Info*);
using AccountResultFn = void (*)(uint64_t, const RunResult&);

int RunSelectedProcessPass(uint64_t now_tick,
                           proc::BootFileLookup lookup,
                           SelectInfoFn select_info,
                           AccountResultFn account_result,
                           PassStopPolicy stop_policy,
                           RunResult* out_results,
                           int max_results) {
    if (lookup == nullptr || select_info == nullptr || out_results == nullptr || max_results <= 0) {
        return 0;
    }
    int ran = 0;
    while (ran < max_results) {
        proc::Info info{};
        if (!select_info(&info)) {
            break;
        }
        RunProcessAndCollectResult(info, lookup, &out_results[ran]);
        if (account_result != nullptr) {
            account_result(now_tick, out_results[ran]);
        }
        ++ran;
        if (ShouldStopPassForPolicy(stop_policy, out_results[ran - 1])) {
            break;
        }
    }
    return ran;
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
    snapshot.last_yield_pid = g_last_yield_pid;
    snapshot.last_yield_tick = g_last_yield_tick;
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
    g_last_yield_pid = 0;
    g_last_yield_tick = 0;
}

WaitStepResult StepWaitProcess(uint32_t pid,
                               bool allow_advance,
                               int64_t* out_exit_code,
                               RunResult* out_result) {
    proc::Info info{};
    if (!proc::GetProcessInfo(pid, &info)) {
        return WaitStepResult::kInvalid;
    }
    if (info.state == proc::State::kExited || info.state == proc::State::kFailed) {
        if (out_exit_code != nullptr) {
            *out_exit_code = info.exit_code;
        }
        return WaitStepResult::kComplete;
    }
    if (!allow_advance || info.state != proc::State::kYielded) {
        return WaitStepResult::kPending;
    }
    RunResult local_result{};
    RunResult* result = out_result != nullptr ? out_result : &local_result;
    if (!RunProcessAndCollectResult(info, nullptr, result)) {
        return WaitStepResult::kPending;
    }
    if (result->final_info.state == proc::State::kExited ||
        result->final_info.state == proc::State::kFailed) {
        if (out_exit_code != nullptr) {
            *out_exit_code = result->final_info.exit_code;
        }
        return WaitStepResult::kComplete;
    }
    return WaitStepResult::kAdvanced;
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
    if (!DequeueNextRunnableInfo(&info)) {
        return false;
    }
    return RunProcessAndCollectResult(info, lookup, out_result);
}

bool RunNextReadyProcess(proc::BootFileLookup lookup, RunResult* out_result) {
    if (lookup == nullptr || out_result == nullptr) {
        return false;
    }
    proc::Info info{};
    if (!DequeueNextReadyInfo(&info)) {
        return false;
    }
    return RunProcessAndCollectResult(info, lookup, out_result);
}

int RunAllReadyProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results) {
    return RunSelectedProcessPass(0, lookup, DequeueNextReadyInfo, nullptr,
                                  PassStopPolicy::kStopOnYielded, out_results, max_results);
}

int RunAllYieldedProcesses(proc::BootFileLookup lookup, RunResult* out_results, int max_results) {
    return RunSelectedProcessPass(0, lookup, DequeueNextYieldedInfo, nullptr,
                                  PassStopPolicy::kStopOnYielded, out_results, max_results);
}

bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info) {
    return DequeueAutoScheduledInfoForTick(now_tick, out_info);
}

int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         RunResult* out_results,
                         int max_results) {
    g_pass_tick_context = now_tick;
    return RunSelectedProcessPass(now_tick, lookup, DequeueAutoScheduledInfoFromContext,
                                  AccountAutoScheduledResult, PassStopPolicy::kStopOnYielded,
                                  out_results, max_results);
}

}  // namespace scheduler
