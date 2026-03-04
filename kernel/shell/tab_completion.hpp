#pragma once

#include "boot_info.h"
#include "shell/context.hpp"

namespace shell {

struct TabCompletionContext {
    const char* command_buffer;
    int command_len;
    int cursor_pos;
    const char* cwd;
    const BootInfo* boot_info;
    const ShellDir* dirs;
    int dir_count;
    const ShellFile* files;
    int file_count;
    const char* const* builtin_commands;
    int builtin_command_count;

    bool (*str_equal)(const char*, const char*);
    bool (*str_starts_with)(const char*, const char*);
    bool (*contains_char)(const char*, char);
    int (*str_length)(const char*);
    void (*copy_string)(char*, const char*, int);
    bool (*get_parent_path)(const char*, char*, int);
    void (*get_base_name)(const char*, char*, int);
    void (*build_boot_file_absolute_path)(const char*, char*, int);
    const ShellFile* (*find_shell_file_by_abs_path)(const char*);
};

enum class TabCompletionAction {
    kNone = 0,
    kReplaceLine,
    kShowCandidates,
};

struct TabCompletionResult {
    TabCompletionAction action;
    char replacement[128];
    char candidates[128][64];
    int candidate_count;
};

void ComputeTabCompletion(const TabCompletionContext& context, TabCompletionResult* out);

}  // namespace shell

