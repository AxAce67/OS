#include "input/ime_session.hpp"

#include "input/ime_logic.hpp"

namespace input {

void ClearCandidateSourceKeys(char source_keys[][32]) {
    if (source_keys == nullptr) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        source_keys[i][0] = '\0';
    }
}

void InitCandidateSourceKeys(const ImeCandidateEntry* entry,
                             char source_keys[][32],
                             void (*copy_string)(char*, const char*, int)) {
    if (entry == nullptr || source_keys == nullptr || copy_string == nullptr) {
        return;
    }
    ClearCandidateSourceKeys(source_keys);
    for (int i = 0; i < entry->count && i < 4; ++i) {
        copy_string(source_keys[i], entry->key, 32);
    }
}

bool StartImeCandidateSession(const ImeCandidateEntry* entry,
                              int cursor_pos,
                              char source_keys[][32],
                              int best_index,
                              int* out_index,
                              int* out_start,
                              int* out_len,
                              bool* out_active,
                              void (*copy_string)(char*, const char*, int)) {
    if (entry == nullptr || entry->count <= 0 ||
        source_keys == nullptr || out_index == nullptr || out_start == nullptr ||
        out_len == nullptr || out_active == nullptr ||
        copy_string == nullptr) {
        return false;
    }
    InitCandidateSourceKeys(entry, source_keys, copy_string);
    *out_index = WrapCandidateIndex(best_index, 0, entry->count);
    *out_start = cursor_pos;
    *out_len = 0;
    *out_active = true;
    return true;
}

bool AdvanceImeCandidateIndex(const ImeCandidateEntry* entry, int* index) {
    if (entry == nullptr || entry->count <= 0 || index == nullptr) {
        return false;
    }
    *index = WrapCandidateIndex(*index, 1, entry->count);
    return true;
}

bool ShouldCycleActiveCandidateOnSpace(char ch,
                                       bool candidate_active,
                                       const ImeCandidateEntry* entry) {
    return ch == ' ' && candidate_active && entry != nullptr && entry->count > 0;
}

const ImeCandidateEntry* ResolveCandidateEntryFromRomaji(
    const char* romaji_buffer,
    int romaji_len,
    char* out_key,
    int out_key_len,
    char (*to_lower_ascii)(char),
    const ImeCandidateEntry* (*find_exact)(const char*)) {
    if (romaji_buffer == nullptr || out_key == nullptr || out_key_len <= 0 ||
        to_lower_ascii == nullptr || find_exact == nullptr ||
        romaji_len <= 0) {
        return nullptr;
    }
    int w = 0;
    for (int i = 0; i < romaji_len && w + 1 < out_key_len; ++i) {
        out_key[w++] = to_lower_ascii(romaji_buffer[i]);
    }
    out_key[w] = '\0';
    if (w <= 0) {
        return nullptr;
    }
    return find_exact(out_key);
}

}  // namespace input
