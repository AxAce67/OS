#pragma once

#include "input/ime_candidate.hpp"

namespace input {

void ClearCandidateSourceKeys(char source_keys[][32]);
void InitCandidateSourceKeys(const ImeCandidateEntry* entry,
                             char source_keys[][32],
                             void (*copy_string)(char*, const char*, int));

bool StartImeCandidateSession(const ImeCandidateEntry* entry,
                              int cursor_pos,
                              char source_keys[][32],
                              int best_index,
                              int* out_index,
                              int* out_start,
                              int* out_len,
                              bool* out_active,
                              void (*copy_string)(char*, const char*, int));

bool AdvanceImeCandidateIndex(const ImeCandidateEntry* entry, int* index);

bool ShouldCycleActiveCandidateOnSpace(char ch,
                                       bool candidate_active,
                                       const ImeCandidateEntry* entry);

const ImeCandidateEntry* ResolveCandidateEntryFromRomaji(
    const char* romaji_buffer,
    int romaji_len,
    char* out_key,
    int out_key_len,
    char (*to_lower_ascii)(char),
    const ImeCandidateEntry* (*find_exact)(const char*));

}  // namespace input
