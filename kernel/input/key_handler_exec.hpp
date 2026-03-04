#pragma once

#include "input/ime_candidate.hpp"
#include "input/key_handler.hpp"

namespace input {

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

}  // namespace input
