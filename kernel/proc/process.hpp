#pragma once

#include <stdint.h>

namespace proc {

enum class State : uint8_t {
    kFree = 0,
    kReady,
    kRunning,
    kExited,
    kFailed,
};

struct Info {
    bool used;
    uint32_t pid;
    State state;
    int64_t exit_code;
    uint64_t start_tick;
    uint64_t end_tick;
    char path[96];
};

bool CreateProcess(const char* path,
                   const char* const* argv, int argc,
                   const char* const* envp, int envc,
                   uint32_t* out_pid);
bool ExecuteProcess(uint32_t pid, const uint8_t* image, uint64_t image_size);
bool IsProcessReady(uint32_t pid);
bool MarkProcessRunning(uint32_t pid);
bool MarkProcessExited(uint32_t pid, int64_t exit_code);
bool MarkProcessFailed(uint32_t pid, int64_t exit_code);
int64_t WaitPid(uint32_t pid, int64_t* out_exit_code, bool nohang);
bool GetProcessInfoByRecentIndex(int recent_index, Info* out_info);
bool FindNextReadyProcess(Info* out_info);
const char* StateName(State state);

bool SetCurrentProcess(uint32_t pid);
void ClearCurrentProcess();
uint32_t GetCurrentProcessId();

const char* const* GetCurrentProcessEnvp();
int GetCurrentProcessEnvc();
bool SetCurrentProcessEnv(const char* key, const char* value);
bool UnsetCurrentProcessEnv(const char* key);

}  // namespace proc
