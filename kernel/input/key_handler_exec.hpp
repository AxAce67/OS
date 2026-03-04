#pragma once

#include "input/ime_candidate.hpp"

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

}  // namespace input

