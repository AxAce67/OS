#include "proc/process.hpp"

#include "arch/x86_64/timer.hpp"
#include "shell/text.hpp"
#include "user/ring3.hpp"

namespace proc {
namespace {

constexpr int kMaxProcesses = 16;
constexpr int kMaxExecEnvs = 24;
constexpr int kMaxExecEnvEntryLen = 128;

struct ProcessEntry {
    Info info;
    int envc;
    char env_entries[kMaxExecEnvs][kMaxExecEnvEntryLen];
    const char* env_ptrs[kMaxExecEnvs];
};

ProcessEntry g_processes[kMaxProcesses];
uint32_t g_next_pid = 1;
int g_next_slot = 0;
uint32_t g_current_pid = 0;
bool g_initialized = false;

void InitIfNeeded() {
    if (g_initialized) {
        return;
    }
    for (int i = 0; i < kMaxProcesses; ++i) {
        g_processes[i].info.used = false;
        g_processes[i].info.pid = 0;
        g_processes[i].info.state = State::kFree;
        g_processes[i].info.exit_code = 0;
        g_processes[i].info.start_tick = 0;
        g_processes[i].info.end_tick = 0;
        g_processes[i].info.path[0] = '\0';
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

}  // namespace

bool CreateProcess(const char* path, const char* const* envp, int envc, uint32_t* out_pid) {
    InitIfNeeded();
    ProcessEntry* entry = &g_processes[g_next_slot];
    g_next_slot = (g_next_slot + 1) % kMaxProcesses;
    entry->info.used = true;
    entry->info.pid = g_next_pid++;
    entry->info.state = State::kReady;
    entry->info.exit_code = 0;
    entry->info.start_tick = 0;
    entry->info.end_tick = 0;
    CopyString(entry->info.path, path != nullptr ? path : "", sizeof(entry->info.path));
    LoadEnv(entry, envp, envc);
    if (out_pid != nullptr) {
        *out_pid = entry->info.pid;
    }
    return true;
}

bool ExecuteProcess(uint32_t pid, const uint8_t* image, uint64_t image_size,
                    const char* const* argv, int argc) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr || image == nullptr) {
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
        image, image_size, argv, argc, entry->env_ptrs, entry->envc);
    if (ok) {
        MarkProcessExited(pid, usermode::GetLastRing3SyscallReturn());
    } else {
        MarkProcessFailed(pid, -1);
    }
    ClearCurrentProcess();
    return ok;
}

bool MarkProcessRunning(uint32_t pid) {
    ProcessEntry* entry = FindEntryByPid(pid);
    if (entry == nullptr) {
        return false;
    }
    entry->info.state = State::kRunning;
    if (entry->info.start_tick == 0) {
        entry->info.start_tick = CurrentTick();
    }
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
    entry->info.end_tick = CurrentTick();
    return true;
}

int64_t WaitPid(uint32_t pid, int64_t* out_exit_code, bool nohang) {
    const ProcessEntry* entry = FindEntryByPidConst(pid);
    if (entry == nullptr) {
        return -1;
    }
    if (entry->info.state == State::kExited || entry->info.state == State::kFailed) {
        if (out_exit_code != nullptr) {
            *out_exit_code = entry->info.exit_code;
        }
        return static_cast<int64_t>(pid);
    }
    if (nohang) {
        return 0;
    }
    return -2;
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
    out_info->exit_code = g_processes[idx].info.exit_code;
    out_info->start_tick = g_processes[idx].info.start_tick;
    out_info->end_tick = g_processes[idx].info.end_tick;
    CopyString(out_info->path, g_processes[idx].info.path, sizeof(out_info->path));
    return true;
}

const char* StateName(State state) {
    switch (state) {
        case State::kReady:
            return "ready";
        case State::kRunning:
            return "running";
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
