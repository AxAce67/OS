#include "shell/cmd_dispatch.hpp"
#include "shell/api.hpp"
#include "shell/text.hpp"

using ShellCommandHandler = bool (*)(const char* cmd, const char* command, const char* rest, int* pos_ptr);

struct ShellCommandEntry {
    const char* name;
    ShellCommandHandler handler;
};

bool HandleHelp(const char*, const char*, const char*, int*) { return ExecuteHelpCommand(); }
bool HandleAbout(const char*, const char*, const char*, int*) { return ExecuteAboutCommand(); }
bool HandlePwd(const char*, const char*, const char*, int*) { return ExecutePwdCommand(); }
bool HandleCd(const char*, const char*, const char* rest, int*) { return ExecuteCdCommand(rest); }
bool HandleMkdir(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteMkdirCommand(command, pos_ptr); }
bool HandleTouch(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteTouchCommand(command, pos_ptr); }
bool HandleWriteAppend(const char* cmd, const char* command, const char*, int* pos_ptr) { return ExecuteWriteAppendCommand(cmd, command, pos_ptr); }
bool HandleCp(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteCpCommand(command, pos_ptr); }
bool HandleRm(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteRmCommand(command, pos_ptr); }
bool HandleRmdir(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteRmdirCommand(command, pos_ptr); }
bool HandleMv(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteMvCommand(command, pos_ptr); }
bool HandleFind(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteFindCommand(command, pos_ptr); }
bool HandleGrep(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteGrepCommand(command, pos_ptr); }
bool HandleClear(const char*, const char*, const char*, int*) { return ExecuteClearCommand(); }
bool HandleTickTime(const char*, const char*, const char*, int*) { return ExecuteTickTimeCommand(); }
bool HandleUptime(const char*, const char*, const char*, int*) { return ExecuteUptimeCommand(); }
bool HandleMem(const char*, const char*, const char*, int*) { return ExecuteMemCommand(); }
bool HandleInputStat(const char*, const char*, const char*, int*) { return ExecuteInputStatCommand(); }
bool HandleRepeat(const char*, const char*, const char* rest, int*) { return ExecuteRepeatCommand(rest); }
bool HandleLayout(const char*, const char*, const char* rest, int*) { return ExecuteLayoutCommand(rest); }
bool HandleIme(const char*, const char*, const char* rest, int*) { return ExecuteImeCommand(rest); }
bool HandleSet(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteSetCommand(command, pos_ptr); }
bool HandleAlias(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteAliasCommand(command, pos_ptr); }
bool HandleSyscall(const char*, const char*, const char* rest, int*) { return ExecuteSyscallCommand(rest); }
bool HandleRing3(const char*, const char*, const char* rest, int*) { return ExecuteRing3Command(rest); }
bool HandleExec(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteExecCommand(command, pos_ptr); }
bool HandleAutoSched(const char*, const char*, const char* rest, int*) { return ExecuteAutoSchedCommand(rest); }
bool HandleRunPid(const char*, const char*, const char* rest, int*) { return ExecuteRunPidCommand(rest); }
bool HandleRunNext(const char*, const char*, const char*, int*) { return ExecuteRunNextCommand(); }
bool HandleRunAll(const char*, const char*, const char*, int*) { return ExecuteRunAllCommand(); }
bool HandleRunPass(const char*, const char*, const char*, int*) { return ExecuteRunAllCommand(); }
bool HandleResumeAll(const char*, const char*, const char*, int*) { return ExecuteResumeAllCommand(); }
bool HandleProcs(const char*, const char*, const char*, int*) { return ExecuteProcsCommand(); }
bool HandleProcQueue(const char*, const char*, const char*, int*) { return ExecuteProcQueueCommand(); }
bool HandleLs(const char*, const char*, const char* rest, int*) { return ExecuteLsCommand(rest); }
bool HandleStat(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteStatCommand(command, pos_ptr); }
bool HandleCat(const char*, const char* command, const char*, int* pos_ptr) { return ExecuteCatCommand(command, pos_ptr); }
bool HandleEcho(const char*, const char*, const char* rest, int*) { return ExecuteEchoCommand(rest); }
bool HandleReboot(const char*, const char*, const char*, int*) { return ExecuteRebootCommand(); }

const ShellCommandEntry kShellCommandTable[] = {
    {"help", HandleHelp},
    {"about", HandleAbout},
    {"pwd", HandlePwd},
    {"cd", HandleCd},
    {"mkdir", HandleMkdir},
    {"touch", HandleTouch},
    {"write", HandleWriteAppend},
    {"append", HandleWriteAppend},
    {"cp", HandleCp},
    {"rm", HandleRm},
    {"rmdir", HandleRmdir},
    {"mv", HandleMv},
    {"find", HandleFind},
    {"grep", HandleGrep},
    {"clear", HandleClear},
    {"tick", HandleTickTime},
    {"time", HandleTickTime},
    {"uptime", HandleUptime},
    {"mem", HandleMem},
    {"inputstat", HandleInputStat},
    {"repeat", HandleRepeat},
    {"layout", HandleLayout},
    {"ime", HandleIme},
    {"set", HandleSet},
    {"alias", HandleAlias},
    {"syscall", HandleSyscall},
    {"ring3", HandleRing3},
    {"exec", HandleExec},
    {"autosched", HandleAutoSched},
    {"runpid", HandleRunPid},
    {"runnext", HandleRunNext},
    {"runpass", HandleRunPass},
    {"runall", HandleRunAll},
    {"resumeall", HandleResumeAll},
    {"procs", HandleProcs},
    {"procq", HandleProcQueue},
    {"ls", HandleLs},
    {"stat", HandleStat},
    {"cat", HandleCat},
    {"echo", HandleEcho},
    {"reboot", HandleReboot},
};

bool DispatchShellCommand(const char* cmd, const char* command, const char* rest, int* pos_ptr) {
    for (int i = 0; i < static_cast<int>(sizeof(kShellCommandTable) / sizeof(kShellCommandTable[0])); ++i) {
        const ShellCommandEntry& entry = kShellCommandTable[i];
        if (!StrEqual(cmd, entry.name)) {
            continue;
        }
        return entry.handler(cmd, command, rest, pos_ptr);
    }
    return false;
}
