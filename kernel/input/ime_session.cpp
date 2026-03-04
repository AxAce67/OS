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

bool ShouldCommitActiveCandidateBeforeShortcut(bool candidate_active, uint8_t key) {
    if (!candidate_active) {
        return false;
    }
    // keep candidate selection on Space and Esc
    return key != 0x39 && key != 0x01;
}

int RestoreRomajiFromActiveCandidate(const ImeCandidateEntry* entry,
                                     char* romaji_buffer,
                                     int romaji_capacity,
                                     int (*str_length)(const char*)) {
    if (entry == nullptr || entry->key == nullptr ||
        romaji_buffer == nullptr || romaji_capacity <= 0 ||
        str_length == nullptr) {
        return 0;
    }
    const int src_len = str_length(entry->key);
    int len = 0;
    for (int i = 0; i < src_len && i + 1 < romaji_capacity; ++i) {
        romaji_buffer[i] = entry->key[i];
        len = i + 1;
    }
    romaji_buffer[len] = '\0';
    return len;
}

ImeCharDecision DecideImeCharHandling(char ch,
                                      bool ime_enabled,
                                      bool jp_layout,
                                      bool has_halfwidth_kana_font,
                                      bool candidate_active,
                                      const ImeCandidateEntry* entry,
                                      int romaji_len,
                                      char (*to_lower_ascii)(char)) {
    ImeCharDecision d{};
    d.ime_path = ime_enabled && jp_layout && has_halfwidth_kana_font;
    d.cycle_candidate = false;
    d.commit_candidate = false;
    d.append_alpha = false;
    d.try_start_candidate = false;
    d.finalize_romaji = false;
    d.lower_alpha = '\0';
    if (!d.ime_path) {
        return d;
    }

    d.cycle_candidate = ShouldCycleActiveCandidateOnSpace(ch, candidate_active, entry);
    d.commit_candidate = candidate_active && ch != ' ';

    const char lower = (to_lower_ascii != nullptr) ? to_lower_ascii(ch) : ch;
    const bool is_alpha = (lower >= 'a' && lower <= 'z');
    if (is_alpha) {
        d.append_alpha = true;
        d.lower_alpha = lower;
        return d;
    }

    if (ch == ' ' && romaji_len > 0) {
        d.try_start_candidate = true;
        return d;
    }

    d.finalize_romaji = true;
    return d;
}

}  // namespace input
