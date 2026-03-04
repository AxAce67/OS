#pragma once

#include <stdint.h>

#include "input/ime_candidate.hpp"

namespace input {

const ImeCandidateEntry* BuildPrefixCandidateEntry(
    const char* prefix,
    const ImeCandidateEntry* user_views,
    int user_count,
    const ImeCandidateEntry* base_table,
    int base_count,
    ImeCandidateEntry* out_view,
    char* out_key,
    int out_key_len,
    const char** out_ptrs,
    char out_texts[][32],
    char out_source_keys[][32],
    bool (*str_starts_with)(const char*, const char*),
    int (*str_length)(const char*),
    bool (*str_equal)(const char*, const char*),
    void (*copy_string)(char*, const char*, int),
    uint16_t (*get_score)(const char* key, const char* cand));

}  // namespace input

