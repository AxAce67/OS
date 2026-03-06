#include "proc/scheduler.hpp"
#include "shell/text.hpp"

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

int RunAutoScheduledTick(uint64_t now_tick,
                         proc::BootFileLookup lookup,
                         TickRunResult* out_results,
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
        out_results[ran].queued_info.used = info.used;
        out_results[ran].queued_info.pid = info.pid;
        out_results[ran].queued_info.state = info.state;
        out_results[ran].queued_info.argc = info.argc;
        out_results[ran].queued_info.exit_code = info.exit_code;
        out_results[ran].queued_info.start_tick = info.start_tick;
        out_results[ran].queued_info.end_tick = info.end_tick;
        CopyString(out_results[ran].queued_info.path, info.path, sizeof(out_results[ran].queued_info.path));
        out_results[ran].final_info.used = false;
        out_results[ran].final_info.pid = info.pid;
        out_results[ran].final_info.state = proc::State::kFree;
        out_results[ran].final_info.argc = 0;
        out_results[ran].final_info.exit_code = 0;
        out_results[ran].final_info.start_tick = 0;
        out_results[ran].final_info.end_tick = 0;
        out_results[ran].final_info.path[0] = '\0';
        out_results[ran].wait_status = 0;
        out_results[ran].ok = proc::RunProcessByPid(info.pid, lookup, &out_results[ran].wait_status);
        if (!proc::GetProcessInfo(info.pid, &out_results[ran].final_info)) {
            out_results[ran].final_info.used = out_results[ran].queued_info.used;
            out_results[ran].final_info.pid = out_results[ran].queued_info.pid;
            out_results[ran].final_info.state = out_results[ran].queued_info.state;
            out_results[ran].final_info.argc = out_results[ran].queued_info.argc;
            out_results[ran].final_info.exit_code = out_results[ran].queued_info.exit_code;
            out_results[ran].final_info.start_tick = out_results[ran].queued_info.start_tick;
            out_results[ran].final_info.end_tick = out_results[ran].queued_info.end_tick;
            CopyString(out_results[ran].final_info.path,
                       out_results[ran].queued_info.path,
                       sizeof(out_results[ran].final_info.path));
        }
        ++ran;
        if (out_results[ran - 1].final_info.state == proc::State::kYielded) {
            break;
        }
    }
    return ran;
}

}  // namespace scheduler
