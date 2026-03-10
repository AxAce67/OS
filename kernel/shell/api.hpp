#pragma once

#include "proc/scheduler.hpp"

bool ExecuteHelpCommand();
bool ExecuteAboutCommand();
bool ExecutePwdCommand();
bool ExecuteCdCommand(const char* rest);
bool ExecuteMkdirCommand(const char* command, int* pos_ptr);
bool ExecuteTouchCommand(const char* command, int* pos_ptr);
bool ExecuteWriteAppendCommand(const char* cmd, const char* command, int* pos_ptr);
bool ExecuteCpCommand(const char* command, int* pos_ptr);
bool ExecuteRmCommand(const char* command, int* pos_ptr);
bool ExecuteRmdirCommand(const char* command, int* pos_ptr);
bool ExecuteMvCommand(const char* command, int* pos_ptr);
bool ExecuteFindCommand(const char* command, int* pos_ptr);
bool ExecuteGrepCommand(const char* command, int* pos_ptr);
bool ExecuteClearCommand();
bool ExecuteTickTimeCommand();
bool ExecuteUptimeCommand();
bool ExecuteMemCommand();
bool ExecuteInputStatCommand();
bool ExecuteInputDiagCommand();
bool ExecuteRepeatCommand(const char* rest);
bool ExecuteLayoutCommand(const char* rest);
bool ExecuteImeCommand(const char* rest);
bool ExecuteSetCommand(const char* command, int* pos_ptr);
bool ExecuteAliasCommand(const char* command, int* pos_ptr);
bool ExecuteLsCommand(const char* rest);
bool ExecuteStatCommand(const char* command, int* pos_ptr);
bool ExecuteCatCommand(const char* command, int* pos_ptr);
bool ExecuteEchoCommand(const char* rest);
bool ExecuteRebootCommand();
bool ExecuteSyscallCommand(const char* rest);
bool ExecuteRing3Command(const char* rest);
bool ExecuteExecCommand(const char* command, int* pos_ptr);
bool ExecuteProcsCommand();
bool ExecuteProcQueueCommand();
bool ExecuteSchedResetCommand();
bool ExecuteRunNextCommand();
bool ExecuteRunAllCommand();
bool ExecuteResumeAllCommand();
bool ExecuteRunPidCommand(const char* rest);
bool ExecuteAutoSchedCommand(const char* rest);

void PrintRunResultStart(const char* prefix, const scheduler::RunResult& result);
void PrintRunResultLine(const char* prefix, const scheduler::RunResult& result);
