#pragma once

#include <stdint.h>
#include "arch/x86_64/syscall_entry.hpp"
#include "boot_info.h"

namespace proc {

using BootFileLookup = const BootFileEntry* (*)(const char* cwd, const char* input_path);

enum class State : uint8_t {
    kFree = 0,
    kReady,
    kRunning,
    kYielded,
    kExited,
    kFailed,
};

struct Info {
    bool used;
    uint32_t pid;
    State state;
    int argc;
    uint32_t yield_count;
    uint32_t resume_count;
    int64_t exit_code;
    uint64_t start_tick;
    uint64_t end_tick;
    char path[96];
};

struct Summary {
    int total;
    int runnable;
    int ready;
    int yielded;
};

bool CreateProcess(const char* path,
                   const char* const* argv, int argc,
                   const char* const* envp, int envc,
                   uint32_t* out_pid);
bool ExecuteProcess(uint32_t pid, const uint8_t* image, uint64_t image_size);
bool RunProcessByPid(uint32_t pid, BootFileLookup lookup, int64_t* out_wait_status);
bool RunNextRunnableProcess(BootFileLookup lookup, Info* out_info, int64_t* out_wait_status);
bool IsProcessRunnable(uint32_t pid);
bool SaveCurrentProcessUserFrame(const Ring3SyscallFrame* frame);
bool MarkProcessRunning(uint32_t pid);
bool MarkProcessYielded(uint32_t pid);
bool MarkProcessExited(uint32_t pid, int64_t exit_code);
bool MarkProcessFailed(uint32_t pid, int64_t exit_code);
int64_t WaitPid(uint32_t pid, int64_t* out_exit_code, bool nohang);
bool GetProcessInfo(uint32_t pid, Info* out_info);
bool GetProcessInfoByRecentIndex(int recent_index, Info* out_info);
bool FindNextRunnableProcess(Info* out_info);
bool FindNextYieldedProcess(Info* out_info);
Summary GetProcessSummary();
const char* StateName(State state);

bool SetCurrentProcess(uint32_t pid);
void ClearCurrentProcess();
uint32_t GetCurrentProcessId();

const char* const* GetCurrentProcessEnvp();
int GetCurrentProcessEnvc();
bool SetCurrentProcessEnv(const char* key, const char* value);
bool UnsetCurrentProcessEnv(const char* key);

}  // namespace proc
