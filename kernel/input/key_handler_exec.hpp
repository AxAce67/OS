#pragma once

#include "input/ime_candidate.hpp"
#include "input/key_handler.hpp"

namespace input {

struct EscCancelState {
    int delete_start;
    int delete_len;
    int cursor_after_delete;
    int restored_romaji_len;
    bool valid;
};

void ClearRomajiInput(char* romaji_buffer, int romaji_capacity, int* romaji_len);

void ResetLineForClear(char* command_buffer,
                       int command_capacity,
                       int* command_len,
                       int* cursor_pos,
                       int* rendered_len,
                       char* romaji_buffer,
                       int romaji_capacity,
                       int* romaji_len);

int RestoreRomajiFromCandidate(const ImeCandidateEntry* entry,
                               char* romaji_buffer,
                               int romaji_capacity,
                               int (*str_length)(const char*));

EscCancelState BuildEscCancelState(const ImeCandidateEntry* entry,
                                   int candidate_start,
                                   int candidate_len,
                                   char* romaji_buffer,
                                   int romaji_capacity,
                                   int (*str_length)(const char*));

void ResetForCtrlL(char* command_buffer,
                   int command_capacity,
                   int* command_len,
                   int* cursor_pos,
                   int* rendered_len,
                   char* romaji_buffer,
                   int romaji_capacity,
                   int* romaji_len);

void ApplyImeModeState(const ImeModeState& mode,
                       bool* ime_enabled,
                       bool* jp_layout);

bool ShouldBrowseHistoryAfterCycle(bool cycle_succeeded);

bool ExecuteRegularNeutralAction(RegularExecKind kind,
                                 int* cursor_pos,
                                 int command_len);

enum class RegularImeExecResult {
    kNotHandled = 0,
    kHandledNeedsRender,
    kFailed,
};

struct RegularImeActionContext {
    void* owner;
    const ImeCandidateEntry* candidate_entry;
    int candidate_start;
    int candidate_len;
    char* romaji_buffer;
    int romaji_capacity;
    int* romaji_len;
    int (*str_length)(const char*);
    bool (*delete_range)(void* owner, int start, int len);
    void (*clear_candidate)(void* owner);
};

RegularImeExecResult ExecuteRegularImeActionWithContext(RegularExecKind kind,
                                                        const RegularImeActionContext& context,
                                                        int* cursor_pos);

struct RegularClearContext {
    void* owner;
    char* command_buffer;
    int command_capacity;
    int* command_len;
    int* cursor_pos;
    int* rendered_len;
    char* romaji_buffer;
    int romaji_capacity;
    int* romaji_len;
    void (*clear_console)(void* owner);
    void (*print_prompt_and_capture_origin)(void* owner);
    void (*clear_candidate)(void* owner);
    void (*clear_selection)(void* owner);
    void (*reset_history_navigation)(void* owner);
};

bool ExecuteRegularClearActionWithContext(RegularExecKind kind,
                                          const RegularClearContext& context);

struct RegularModeContext {
    void* owner;
    RegularShortcutAction mode_action;
    bool* ime_enabled;
    bool* jp_layout;
    void (*repaint_prompt_and_input)(void* owner);
};

bool ExecuteRegularModeActionWithContext(RegularExecKind kind,
                                         const RegularModeContext& context);

struct RegularActionContext {
    void* owner;
    bool (*cycle_candidate)(void* owner, int direction);
    void (*browse_history_up)(void* owner);
    void (*browse_history_down)(void* owner);
    void (*backspace_at_cursor)(void* owner);
    void (*delete_at_cursor)(void* owner);
    void (*tab_complete)(void* owner);
};

bool ExecuteRegularActionWithContext(RegularExecKind kind,
                                     const RegularActionContext& context);

struct ExtendedCursorMoveResult {
    bool handled;
    bool should_render;
};

struct ExtendedActionContext {
    void* owner;
    void (*scroll_up)(void* owner, int lines);
    void (*scroll_down)(void* owner, int lines);
    void (*delete_at_cursor)(void* owner);
    void (*browse_history_up)(void* owner);
    void (*browse_history_down)(void* owner);
};

bool ExecuteExtendedActionWithContext(ExtendedExecKind kind,
                                      const ExtendedActionContext& context);

ExtendedCursorMoveResult ExecuteExtendedCursorMoveAction(ExtendedExecKind kind,
                                                         int* cursor_pos,
                                                         int command_len);

}  // namespace input
