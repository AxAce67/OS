#include "proc/process.hpp"

#include "arch/x86_64/timer.hpp"
#include "proc/scheduler.hpp"
#include "shell/text.hpp"
#include "user/ring3.hpp"

namespace proc {
namespace {

constexpr int kMaxProcesses = 16;
constexpr int kMaxExecArgs = 16;
constexpr int kMaxExecArgLen = 64;
constexpr int kMaxExecEnvs = 24;
constexpr int kMaxExecEnvEntryLen = 128;

struct ProcessEntry {
    Info info;
    bool has_saved_frame;
    Ring3SyscallFrame saved_frame;
    int argc;
    char arg_entries[kMaxExecArgs][kMaxExecArgLen];
    const char* arg_ptrs[kMaxExecArgs];
    int envc;
    char env_entries[kMaxExecEnvs][kMaxExecEnvEntryLen];
    const char* env_ptrs[kMaxExecEnvs];
};

ProcessEntry g_processes[kMaxProcesses];
uint32_t g_next_pid = 1;
int g_next_slot = 0;
int g_next_runnable_slot = 0;
int g_next_yielded_slot = 0;
uint32_t g_current_pid = 0;
bool g_initialized = false;

bool IsRunnableState(State state) {
    return state == State::kReady || state == State::kYielded;
}

void CopyInfoFromEntry(Info* out_info, const ProcessEntry* entry) {
    if (out_info == nullptr || entry == nullptr) {
        return;
    }
    out_info->used = entry->info.used;
    out_info->pid = entry->info.pid;
    out_info->state = entry->info.state;
    out_info->argc = entry->info.argc;
    out_info->yield_count = entry->info.yield_count;
    out_info->resume_count = entry->info.resume_count;
    out_info->exit_code = entry->info.exit_code;
    out_info->start_tick = entry->info.start_tick;
    out_info->end_tick = entry->info.end_tick;
    CopyString(out_info->path, entry->info.path, sizeof(out_info->path));
}

void InitIfNeeded() {
    if (g_initialized) {
        return;
    }
    for (int i = 0; i < kMaxProcesses; ++i) {
        g_processes[i].info.used = false;
        g_processes[i].info.pid = 0;
        g_processes[i].info.state = State::kFree;
        g_processes[i].info.argc = 0;
        g_processes[i].info.yield_count = 0;
        g_processes[i].info.resume_count = 0;
        g_processes[i].info.exit_code = 0;
        g_processes[i].info.start_tick = 0;
        g_processes[i].info.end_tick = 0;
        g_processes[i].info.path[0] = '\0';
        g_processes[i].has_saved_frame = false;
        g_processes[i].argc = 0;
        for (int j = 0; j < kMaxExecArgs; ++j) {
            g_processes[i].arg_entries[j][0] = '\0';
            g_processes[i].arg_ptrs[j] = nullptr;
        }
        g_processes[i].envc = 0;
        for (int j = 0; j < kMaxExecEnvs; ++j) {
            g_processes[i].env_entries[j][0] = '\0';
            g_processes[i].env_ptrs[j] = nullptr;
        }
    }
    g_initialized = true;
}

ProcessEntry* FindEntryByPid(uint32_t pid) {
    if (pid == 0) {
        return nullptr;
    }
    InitIfNeeded();
    for (int i = 0; i < kMaxProcesses; ++i) {
        if (!g_processes[i].info.used || g_processes[i].info.pid != pid) {
            continue;
        }
        return &g_processes[i];
    }
    return nullptr;
}

const ProcessEntry* FindEntryByPidConst(uint32_t pid) {
    return FindEntryByPid(pid);
}

int FindEqPos(const char* s) {
    if (s == nullptr) {
        return -1;
    }
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '=') {
            return i;
        }
    }
    return -1;
}

bool StrEqLocal(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

bool BuildEnvEntry(char* dst, int dst_len, const char* key, const char* value) {
    if (dst == nullptr || dst_len <= 2 || key == nullptr || value == nullptr) {
        return false;
    }
    int di = 0;
    for (int i = 0; key[i] != '\0' && di + 1 < dst_len; ++i) {
        if (key[i] == '=') {
            return false;
        }
        dst[di++] = key[i];
    }
    if (di == 0 || di + 1 >= dst_len) {
        return false;
    }
    dst[di++] = '=';
    for (int i = 0; value[i] != '\0' && di + 1 < dst_len; ++i) {
        dst[di++] = value[i];
    }
    dst[di] = '\0';
    return true;
}

bool SplitEnvEntry(const char* entry, char* out_key, int out_key_len, const char** out_value) {
    if (entry == nullptr || out_key == nullptr || out_key_len <= 1 || out_value == nullptr) {
        return false;
    }
    const int eq = FindEqPos(entry);
    if (eq <= 0 || eq >= out_key_len) {
        return false;
    }
    for (int i = 0; i < eq; ++i) {
        out_key[i] = entry[i];
    }
    out_key[eq] = '\0';
    *out_value = entry + eq + 1;
    return true;
}

void ClearEnv(ProcessEntry* entry) {
    if (entry == nullptr) {
        return;
    }
    entry->envc = 0;
    for (int i = 0; i < kMaxExecEnvs; ++i) {
        entry->env_entries[i][0] = '\0';
        entry->env_ptrs[i] = nullptr;
    }
}

void ClearArgs(ProcessEntry* entry) {
    if (entry == nullptr) {
        return;
    }
    entry->argc = 0;
    for (int i = 0; i < kMaxExecArgs; ++i) {
        entry->arg_entries[i][0] = '\0';
        entry->arg_ptrs[i] = nullptr;
    }
}

void LoadArgs(ProcessEntry* entry, const char* const* argv, int argc) {
    ClearArgs(entry);
    if (entry == nullptr || argv == nullptr || argc <= 0) {
        return;
    }
    if (argc > kMaxExecArgs) {
        argc = kMaxExecArgs;
    }
    for (int i = 0; i < argc; ++i) {
        const char* src = argv[i];
        if (src == nullptr) {
            src = "";
        }
        CopyString(entry->arg_entries[i], src, kMaxExecArgLen);
        entry->arg_ptrs[i] = entry->arg_entries[i];
    }
    entry->argc = argc;
}

void LoadEnv(ProcessEntry* entry, const char* const* envp, int envc) {
    ClearEnv(entry);
    if (entry == nullptr || envp == nullptr || envc <= 0) {
        return;
    }
    if (envc > kMaxExecEnvs) {
        envc = kMaxExecEnvs;
    }
    int loaded = 0;
    for (int i = 0; i < envc; ++i) {
        const char* src = envp[i];
        if (src == nullptr || src[0] == '\0') {
            continue;
        }
        int len = 0;
        while (src[len] != '\0' && len + 1 < kMaxExecEnvEntryLen) {
            entry->env_entries[loaded][len] = src[len];
            ++len;
        }
        if (src[len] != '\0') {
            continue;
        }
        entry->env_entries[loaded][len] = '\0';
        entry->env_ptrs[loaded] = entry->env_entries[loaded];
        ++loaded;
        if (loaded >= kMaxExecEnvs) {
            break;
        }
    }
    entry->envc = loaded;
}

ProcessEntry* GetCurrentEntry() {
    return FindEntryByPid(g_current_pid);
}

bool IsWaitCompleteState(State state) {
    return state == State::kExited || state == State::kFailed;
}

bool CanAdvanceWithoutLookup(const ProcessEntry* entry) {
    return entry != nullptr && entry->info.state == State::kYielded && entry->has_saved_frame;
}

void CpuPause() {
    __asm__ volatile("pause");
}

void CopySavedFrame(Ring3SyscallFrame* dst, const Ring3SyscallFrame* src) {
    if (dst == nullptr || src == nullptr) {
        return;
    }
    dst->r15 = src->r15;
    dst->r14 = src->r14;
    dst->r13 = src->r13;
    dst->r12 = src->r12;
    dst->r11 = src->r11;
    dst->r10 = src->r10;
    dst->r9 = src->r9;
    dst->r8 = src->r8;
    dst->rbp = src->rbp;
    dst->rdi = src->rdi;
    dst->rsi = src->rsi;
    dst->rdx = src->rdx;
    dst->rcx = src->rcx;
    dst->rbx = src->rbx;
    dst->rax = src->rax;
    dst->rip = src->rip;
    dst->cs = src->cs;
    dst->rflags = src->rflags;
    dst->rsp = src->rsp;
    dst->ss = src->ss;
}

void ClearSavedFrame(ProcessEntry* entry) {
    if (entry == nullptr) {
        return;
    }
    entry->has_saved_frame = false;
    entry->saved_frame.rax = 0;
    entry->saved_frame.rbx = 0;
    entry->saved_frame.rcx = 0;
    entry->saved_frame.rdx = 0;
    entry->saved_frame.rsi = 0;
    entry->saved_frame.rdi = 0;
    entry->saved_frame.rbp = 0;
    entry->saved_frame.r8 = 0;
    entry->saved_frame.r9 = 0;
    entry->saved_frame.r10 = 0;
    entry->saved_frame.r11 = 0;
    entry->saved_frame.r12 = 0;
    entry->saved_frame.r13 = 0;
    entry->saved_frame.r14 = 0;
    entry->saved_frame.r15 = 0;
    entry->saved_frame.rip = 0;
    entry->saved_frame.cs = 0;
    entry->saved_frame.rflags = 0;
    entry->saved_frame.rsp = 0;
    entry->saved_frame.ss = 0;
}

bool CompleteProcessRun(ProcessEntry* entry, bool ok) {
    if (entry == nullptr) {
        return false;
    }
    const Ring3ReturnReason reason = usermode::GetLastRing3ReturnReason();
    if (reason == Ring3ReturnReason::kYield) {
        return MarkProcessYielded(entry->info.pid);
    }
    ClearSavedFrame(entry);
    if (ok) {
        return MarkProcessExited(entry->info.pid, usermode::GetLastRing3SyscallReturn());
    }
    return MarkProcessFailed(entry->info.pid, -1);
}

}  // namespace

bool CreateProcess(const char* path,
                   const char* const* argv, int argc,
                   const char* const* envp, int envc,
                   uint32_t* out_pid) {
    InitIfNeeded();
    ProcessEntry* entry = &g_processes[g_next_slot];
    g_next_slot = (g_next_slot + 1) % kMaxProcesses;
    entry->info.used = true;
    entry->info.pid = g_next_pid++;
    entry->info.state = State::kReady;
    entry->info.argc = argc;
    entry->info.yield_count = 0;
    entry->info.resume_count = 0;
    entry->info.exit_code = 0;
    entry->info.start_tick = 0;
    entry->info.end_tick = 0;
    CopyString(entry->info.path, path != nullptr ? path : "", sizeof(entry->info.path));
    ClearSavedFrame(entry);
    LoadArgs(entry, argv, argc);
    LoadEnv(entry, envp, envc);
    if (out_pid != nullptr) {
        *out_pid = entry->info.pid;
    }
    return true;
}

bool ExecuteProcess(uint32_t pid, const uint8_t* image, uint64_t image_size) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr || image == nullptr || entry->info.state != State::kReady) {
        return false;
    }
    if (!MarkProcessRunning(pid)) {
        return false;
    }
    if (!SetCurrentProcess(pid)) {
        MarkProcessFailed(pid, -1);
        return false;
    }
    const bool ok = usermode::RunRing3BinaryFromBufferWithContext(
        image, image_size, entry->arg_ptrs, entry->argc, entry->env_ptrs, entry->envc);
    CompleteProcessRun(entry, ok);
    ClearCurrentProcess();
    return ok;
}

bool ResumeProcess(uint32_t pid) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr || entry->info.state != State::kYielded || !entry->has_saved_frame) {
        return false;
    }
    if (!MarkProcessRunning(pid)) {
        return false;
    }
    if (!SetCurrentProcess(pid)) {
        MarkProcessFailed(pid, -1);
        return false;
    }
    ResumeUserModeFrame(&entry->saved_frame);
    const bool ok = usermode::FinalizeLastRing3Run();
    CompleteProcessRun(entry, ok);
    ClearCurrentProcess();
    return ok;
}

bool RunProcessByPid(uint32_t pid, BootFileLookup lookup, int64_t* out_wait_status) {
    const ProcessEntry* entry = FindEntryByPidConst(pid);
    if (entry == nullptr) {
        return false;
    }
    if (entry->info.state == State::kReady) {
        if (lookup == nullptr) {
            return false;
        }
        const BootFileEntry* file = lookup("/", entry->info.path);
        if (file == nullptr) {
            MarkProcessFailed(pid, -1);
            return false;
        }
        if (!ExecuteProcess(pid, file->data, file->size)) {
            MarkProcessFailed(pid, -1);
            return false;
        }
    } else if (entry->info.state == State::kYielded) {
        if (!ResumeProcess(pid)) {
            MarkProcessFailed(pid, -1);
            return false;
        }
    } else {
        return false;
    }
    entry = FindEntryByPidConst(pid);
    if (entry != nullptr && entry->info.state == State::kYielded) {
        if (out_wait_status != nullptr) {
            *out_wait_status = 0;
        }
        return true;
    }
    int64_t wait_status = 0;
    const int64_t wait_ret = WaitPid(pid, &wait_status, false);
    if (wait_ret <= 0) {
        return false;
    }
    if (out_wait_status != nullptr) {
        *out_wait_status = wait_status;
    }
    return true;
}

bool RunNextRunnableProcess(BootFileLookup lookup, Info* out_info, int64_t* out_wait_status) {
    Info info{};
    if (!FindNextRunnableProcess(&info)) {
        return false;
    }
    if (out_info != nullptr) {
        out_info->used = info.used;
        out_info->pid = info.pid;
        out_info->state = info.state;
        out_info->argc = info.argc;
        out_info->yield_count = info.yield_count;
        out_info->resume_count = info.resume_count;
        out_info->exit_code = info.exit_code;
        out_info->start_tick = info.start_tick;
        out_info->end_tick = info.end_tick;
        CopyString(out_info->path, info.path, sizeof(out_info->path));
    }
    return RunProcessByPid(info.pid, lookup, out_wait_status);
}

bool IsProcessRunnable(uint32_t pid) {
    const ProcessEntry* entry = FindEntryByPidConst(pid);
    return entry != nullptr && IsRunnableState(entry->info.state);
}

bool SaveCurrentProcessUserFrame(const Ring3SyscallFrame* frame) {
    ProcessEntry* entry = GetCurrentEntry();
    if (entry == nullptr || frame == nullptr) {
        return false;
    }
    CopySavedFrame(&entry->saved_frame, frame);
    entry->has_saved_frame = true;
    return true;
}

bool MarkProcessRunning(uint32_t pid) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr) {
        return false;
    }
    if (entry->info.state == State::kYielded) {
        ++entry->info.resume_count;
    }
    entry->info.state = State::kRunning;
    if (entry->info.start_tick == 0) {
        entry->info.start_tick = CurrentTick();
    }
    entry->info.end_tick = 0;
    return true;
}

bool MarkProcessYielded(uint32_t pid) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr) {
        return false;
    }
    ++entry->info.yield_count;
    entry->info.state = State::kYielded;
    entry->info.end_tick = 0;
    return true;
}

bool MarkProcessExited(uint32_t pid, int64_t exit_code) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr) {
        return false;
    }
    entry->info.state = State::kExited;
    entry->info.exit_code = exit_code;
    ClearSavedFrame(entry);
    if (entry->info.start_tick == 0) {
        entry->info.start_tick = CurrentTick();
    }
    entry->info.end_tick = CurrentTick();
    return true;
}

bool MarkProcessFailed(uint32_t pid, int64_t exit_code) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr) {
        return false;
    }
    entry->info.state = State::kFailed;
    entry->info.exit_code = exit_code;
    ClearSavedFrame(entry);
    entry->info.end_tick = CurrentTick();
    return true;
}

int64_t WaitPid(uint32_t pid, int64_t* out_exit_code, bool nohang) {
    if (pid == 0 || pid == g_current_pid) {
        return -1;
    }
    const ProcessEntry* entry = FindEntryByPidConst(pid);
    if (entry == nullptr) {
        return -1;
    }
    if (IsWaitCompleteState(entry->info.state)) {
        if (out_exit_code != nullptr) {
            *out_exit_code = entry->info.exit_code;
        }
        return static_cast<int64_t>(pid);
    }
    if (nohang) {
        return 0;
    }
    while (true) {
        entry = FindEntryByPidConst(pid);
        if (entry == nullptr) {
            return -1;
        }
        if (IsWaitCompleteState(entry->info.state)) {
            if (out_exit_code != nullptr) {
                *out_exit_code = entry->info.exit_code;
            }
            return static_cast<int64_t>(pid);
        }
        if (CanAdvanceWithoutLookup(entry)) {
            scheduler::AdvanceProcessForWait(pid, nullptr);
            continue;
        }
        CpuPause();
    }
}

bool GetProcessInfo(uint32_t pid, Info* out_info) {
    const ProcessEntry* entry = FindEntryByPidConst(pid);
    if (entry == nullptr || out_info == nullptr) {
        return false;
    }
    out_info->used = entry->info.used;
    out_info->pid = entry->info.pid;
    out_info->state = entry->info.state;
    out_info->argc = entry->info.argc;
    out_info->yield_count = entry->info.yield_count;
    out_info->resume_count = entry->info.resume_count;
    out_info->exit_code = entry->info.exit_code;
    out_info->start_tick = entry->info.start_tick;
    out_info->end_tick = entry->info.end_tick;
    CopyString(out_info->path, entry->info.path, sizeof(out_info->path));
    return true;
}

bool GetProcessInfoByRecentIndex(int recent_index, Info* out_info) {
    InitIfNeeded();
    if (recent_index < 0 || recent_index >= kMaxProcesses || out_info == nullptr) {
        return false;
    }
    const int idx = (g_next_slot - 1 - recent_index + kMaxProcesses) % kMaxProcesses;
    if (!g_processes[idx].info.used) {
        return false;
    }
    out_info->used = g_processes[idx].info.used;
    out_info->pid = g_processes[idx].info.pid;
    out_info->state = g_processes[idx].info.state;
    out_info->argc = g_processes[idx].info.argc;
    out_info->yield_count = g_processes[idx].info.yield_count;
    out_info->resume_count = g_processes[idx].info.resume_count;
    out_info->exit_code = g_processes[idx].info.exit_code;
    out_info->start_tick = g_processes[idx].info.start_tick;
    out_info->end_tick = g_processes[idx].info.end_tick;
    CopyString(out_info->path, g_processes[idx].info.path, sizeof(out_info->path));
    return true;
}

bool PeekNextRunnableProcess(Info* out_info) {
    InitIfNeeded();
    if (out_info == nullptr) {
        return false;
    }
    for (int n = 0; n < kMaxProcesses; ++n) {
        const int idx = (g_next_runnable_slot + n) % kMaxProcesses;
        if (!g_processes[idx].info.used) {
            continue;
        }
        if (!IsRunnableState(g_processes[idx].info.state)) {
            continue;
        }
        CopyInfoFromEntry(out_info, &g_processes[idx]);
        return true;
    }
    return false;
}

void AdvanceRunnableProcessCursor() {
    g_next_runnable_slot = (g_next_runnable_slot + 1) % kMaxProcesses;
}

bool FindNextRunnableProcess(Info* out_info) {
    if (!PeekNextRunnableProcess(out_info)) {
        return false;
    }
    AdvanceRunnableProcessCursor();
    return true;
}

bool PeekNextYieldedProcess(Info* out_info) {
    InitIfNeeded();
    if (out_info == nullptr) {
        return false;
    }
    for (int n = 0; n < kMaxProcesses; ++n) {
        const int idx = (g_next_yielded_slot + n) % kMaxProcesses;
        if (!g_processes[idx].info.used) {
            continue;
        }
        if (g_processes[idx].info.state != State::kYielded) {
            continue;
        }
        CopyInfoFromEntry(out_info, &g_processes[idx]);
        return true;
    }
    return false;
}

void AdvanceYieldedProcessCursor() {
    g_next_yielded_slot = (g_next_yielded_slot + 1) % kMaxProcesses;
}

bool FindNextYieldedProcess(Info* out_info) {
    if (!PeekNextYieldedProcess(out_info)) {
        return false;
    }
    AdvanceYieldedProcessCursor();
    return true;
}

Summary GetProcessSummary() {
    InitIfNeeded();
    Summary summary{};
    summary.total = 0;
    summary.runnable = 0;
    summary.ready = 0;
    summary.yielded = 0;
    for (int i = 0; i < kMaxProcesses; ++i) {
        if (!g_processes[i].info.used) {
            continue;
        }
        ++summary.total;
        if (IsRunnableState(g_processes[i].info.state)) {
            ++summary.runnable;
        }
        if (g_processes[i].info.state == State::kReady) {
            ++summary.ready;
        } else if (g_processes[i].info.state == State::kYielded) {
            ++summary.yielded;
        }
    }
    return summary;
}

const char* StateName(State state) {
    switch (state) {
        case State::kReady:
            return "ready";
        case State::kRunning:
            return "running";
        case State::kYielded:
            return "yielded";
        case State::kExited:
            return "exited";
        case State::kFailed:
            return "failed";
        case State::kFree:
        default:
            return "free";
    }
}

bool SetCurrentProcess(uint32_t pid) {
    if (pid == 0) {
        g_current_pid = 0;
        return true;
    }
    if (FindEntryByPid(pid) == nullptr) {
        return false;
    }
    g_current_pid = pid;
    return true;
}

void ClearCurrentProcess() {
    g_current_pid = 0;
}

uint32_t GetCurrentProcessId() {
    return g_current_pid;
}

const char* const* GetCurrentProcessEnvp() {
    const ProcessEntry* entry = FindEntryByPidConst(g_current_pid);
    if (entry == nullptr || entry->envc <= 0) {
        return nullptr;
    }
    return entry->env_ptrs;
}

int GetCurrentProcessEnvc() {
    const ProcessEntry* entry = FindEntryByPidConst(g_current_pid);
    if (entry == nullptr) {
        return 0;
    }
    return entry->envc;
}

bool SetCurrentProcessEnv(const char* key, const char* value) {
    ProcessEntry* entry = GetCurrentEntry();
    if (entry == nullptr || key == nullptr || value == nullptr) {
        return false;
    }
    char merged[kMaxExecEnvEntryLen];
    if (!BuildEnvEntry(merged, kMaxExecEnvEntryLen, key, value)) {
        return false;
    }
    for (int i = 0; i < entry->envc; ++i) {
        char existing_key[64];
        const char* existing_value = nullptr;
        if (!SplitEnvEntry(entry->env_ptrs[i], existing_key, sizeof(existing_key), &existing_value)) {
            continue;
        }
        (void)existing_value;
        if (!StrEqLocal(existing_key, key)) {
            continue;
        }
        CopyString(entry->env_entries[i], merged, kMaxExecEnvEntryLen);
        entry->env_ptrs[i] = entry->env_entries[i];
        return true;
    }
    if (entry->envc >= kMaxExecEnvs) {
        return false;
    }
    CopyString(entry->env_entries[entry->envc], merged, kMaxExecEnvEntryLen);
    entry->env_ptrs[entry->envc] = entry->env_entries[entry->envc];
    ++entry->envc;
    return true;
}

bool UnsetCurrentProcessEnv(const char* key) {
    ProcessEntry* entry = GetCurrentEntry();
    if (entry == nullptr || key == nullptr || key[0] == '\0') {
        return false;
    }
    for (int i = 0; i < entry->envc; ++i) {
        char existing_key[64];
        const char* existing_value = nullptr;
        if (!SplitEnvEntry(entry->env_ptrs[i], existing_key, sizeof(existing_key), &existing_value)) {
            continue;
        }
        (void)existing_value;
        if (!StrEqLocal(existing_key, key)) {
            continue;
        }
        for (int j = i; j + 1 < entry->envc; ++j) {
            CopyString(entry->env_entries[j], entry->env_entries[j + 1], kMaxExecEnvEntryLen);
            entry->env_ptrs[j] = entry->env_entries[j];
        }
        --entry->envc;
        entry->env_entries[entry->envc][0] = '\0';
        entry->env_ptrs[entry->envc] = nullptr;
        return true;
    }
    return false;
}

}  // namespace proc
