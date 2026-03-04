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

void SetCursorValue(int* cursor_pos, int target);

bool ShouldBrowseHistoryAfterCycle(bool cycle_succeeded);

struct RegularNeutralCallbacks {
    void (*set_cursor_value)(void* ctx, int target);
};

bool ExecuteRegularNeutralAction(RegularExecKind kind,
                                 int command_len,
                                 const RegularNeutralCallbacks& callbacks,
                                 void* ctx);

struct RegularActionCallbacks {
    bool (*cycle_candidate)(void* ctx, int direction);
    void (*browse_history)(void* ctx, int direction);
    void (*backspace_at_cursor)(void* ctx);
    void (*delete_at_cursor)(void* ctx);
    void (*tab_complete)(void* ctx);
};

bool ExecuteRegularAction(RegularExecKind kind,
                          const RegularActionCallbacks& callbacks,
                          void* ctx);

struct ExtendedCursorMoveResult {
    bool handled;
    bool should_render;
};

struct ExtendedActionCallbacks {
    void (*scroll)(void* ctx, int direction, int lines);
    void (*delete_at_cursor)(void* ctx);
    void (*browse_history)(void* ctx, int direction);
};

bool ExecuteExtendedAction(ExtendedExecKind kind,
                           const ExtendedActionCallbacks& callbacks,
                           void* ctx);

ExtendedCursorMoveResult ExecuteExtendedCursorMoveAction(ExtendedExecKind kind,
                                                         int* cursor_pos,
                                                         int command_len);

}  // namespace input
