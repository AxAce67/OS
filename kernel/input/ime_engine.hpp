#pragma once

#include <stdint.h>

#include "input/ime_candidate.hpp"

namespace input {

struct ImeFlushResult {
    bool inserted;
    bool romaji_changed;
};

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

ImeFlushResult FlushImeRomaji(
    char* romaji_buffer,
    int* romaji_len,
    bool finalize,
    bool has_selection,
    bool (*delete_selection)(void*),
    void* delete_selection_ctx,
    bool (*insert_byte)(void*, uint8_t),
    void* insert_byte_ctx,
    bool (*convert_head_to_kana)(const char* romaji, int romaji_len, bool finalize,
                                 int* consume, uint8_t* kana_bytes, int* kana_len));

}  // namespace input
