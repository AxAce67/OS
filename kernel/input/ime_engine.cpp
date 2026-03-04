#include "input/ime_engine.hpp"

#include "input/ime_logic.hpp"

namespace input {

namespace {

void ResetOut(ImeCandidateEntry* out_view,
              char* out_key,
              int out_key_len,
              const char** out_ptrs,
              char out_texts[][32],
              char out_source_keys[][32],
              void (*copy_string)(char*, const char*, int),
              const char* prefix) {
    copy_string(out_key, prefix, out_key_len);
    out_view->key = out_key;
    out_view->count = 0;
    for (int i = 0; i < 4; ++i) {
        out_ptrs[i] = nullptr;
        out_texts[i][0] = '\0';
        out_source_keys[i][0] = '\0';
        out_view->candidates[i] = nullptr;
    }
}

void AppendUniqueCandidates(const ImeCandidateEntry* src,
                            ImeCandidateEntry* out_view,
                            const char** out_ptrs,
                            char out_texts[][32],
                            char out_source_keys[][32],
                            bool (*str_equal)(const char*, const char*),
                            void (*copy_string)(char*, const char*, int)) {
    if (src == nullptr || src->count <= 0) {
        return;
    }
    for (int i = 0; i < src->count; ++i) {
        const char* cand = src->candidates[i];
        if (cand == nullptr || cand[0] == '\0') {
            continue;
        }
        bool exists = false;
        for (int j = 0; j < out_view->count; ++j) {
            if (str_equal(out_ptrs[j], cand)) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        if (out_view->count >= 4) {
            return;
        }
        const int dst = out_view->count;
        copy_string(out_texts[dst], cand, 32);
        out_ptrs[dst] = out_texts[dst];
        out_view->candidates[dst] = out_ptrs[dst];
        copy_string(out_source_keys[dst], src->key, 32);
        ++out_view->count;
    }
}

}  // namespace

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
    uint16_t (*get_score)(const char* key, const char* cand)) {
    if (prefix == nullptr || prefix[0] == '\0' ||
        out_view == nullptr || out_key == nullptr || out_ptrs == nullptr ||
        out_texts == nullptr || out_source_keys == nullptr ||
        str_starts_with == nullptr || str_length == nullptr ||
        str_equal == nullptr || copy_string == nullptr || get_score == nullptr) {
        return nullptr;
    }

    const int prefix_len = str_length(prefix);
    ResetOut(out_view, out_key, out_key_len, out_ptrs, out_texts, out_source_keys, copy_string, prefix);

    int max_key_len = prefix_len;
    for (int i = 0; i < user_count; ++i) {
        const ImeCandidateEntry* e = &user_views[i];
        if (e->key == nullptr || !str_starts_with(e->key, prefix)) {
            continue;
        }
        const int key_len = str_length(e->key);
        if (key_len > max_key_len) {
            max_key_len = key_len;
        }
    }
    for (int i = 0; i < base_count; ++i) {
        const ImeCandidateEntry* e = &base_table[i];
        if (e->key == nullptr || !str_starts_with(e->key, prefix)) {
            continue;
        }
        const int key_len = str_length(e->key);
        if (key_len > max_key_len) {
            max_key_len = key_len;
        }
    }

    for (int target_len = max_key_len; target_len >= prefix_len; --target_len) {
        for (int i = 0; i < user_count; ++i) {
            const ImeCandidateEntry* e = &user_views[i];
            if (e->key == nullptr || !str_starts_with(e->key, prefix)) {
                continue;
            }
            if (str_length(e->key) != target_len) {
                continue;
            }
            AppendUniqueCandidates(e, out_view, out_ptrs, out_texts, out_source_keys, str_equal, copy_string);
            if (out_view->count >= 4) {
                SortCandidatesByLearning(out_view->candidates, out_source_keys, out_view->count, get_score);
                return out_view;
            }
        }
        for (int i = 0; i < base_count; ++i) {
            const ImeCandidateEntry* e = &base_table[i];
            if (e->key == nullptr || !str_starts_with(e->key, prefix)) {
                continue;
            }
            if (str_length(e->key) != target_len) {
                continue;
            }
            AppendUniqueCandidates(e, out_view, out_ptrs, out_texts, out_source_keys, str_equal, copy_string);
            if (out_view->count >= 4) {
                SortCandidatesByLearning(out_view->candidates, out_source_keys, out_view->count, get_score);
                return out_view;
            }
        }
    }

    if (out_view->count <= 0) {
        return nullptr;
    }
    SortCandidatesByLearning(out_view->candidates, out_source_keys, out_view->count, get_score);
    return out_view;
}

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
                                 int* consume, uint8_t* kana_bytes, int* kana_len)) {
    ImeFlushResult result{false, false};
    if (romaji_buffer == nullptr || romaji_len == nullptr || *romaji_len <= 0 ||
        insert_byte == nullptr || convert_head_to_kana == nullptr) {
        return result;
    }
    const int before_len = *romaji_len;

    if (has_selection && delete_selection != nullptr) {
        delete_selection(delete_selection_ctx);
    }
    while (*romaji_len > 0) {
        int consume = 0;
        uint8_t kana_bytes[3] = {0, 0, 0};
        int kana_len = 0;
        if (!convert_head_to_kana(romaji_buffer, *romaji_len, finalize, &consume, kana_bytes, &kana_len)) {
            break;
        }
        for (int i = 0; i < kana_len; ++i) {
            if (!insert_byte(insert_byte_ctx, kana_bytes[i])) {
                break;
            }
            result.inserted = true;
        }
        for (int i = consume; i <= *romaji_len; ++i) {
            romaji_buffer[i - consume] = romaji_buffer[i];
        }
        *romaji_len -= consume;
        if (*romaji_len < 0) {
            *romaji_len = 0;
            romaji_buffer[0] = '\0';
            break;
        }
    }
    if (finalize && *romaji_len > 0) {
        for (int i = 0; i < *romaji_len; ++i) {
            if (!insert_byte(insert_byte_ctx, static_cast<uint8_t>(romaji_buffer[i]))) {
                break;
            }
            result.inserted = true;
        }
        *romaji_len = 0;
        romaji_buffer[0] = '\0';
    }
    result.romaji_changed = (*romaji_len != before_len);
    return result;
}

const char* ResolveImeLearningKey(const ImeCandidateEntry* entry,
                                  int candidate_index,
                                  const char source_keys[][32]) {
    if (entry == nullptr || candidate_index < 0 || candidate_index >= entry->count) {
        return nullptr;
    }
    if (candidate_index < 4 && source_keys != nullptr && source_keys[candidate_index][0] != '\0') {
        return source_keys[candidate_index];
    }
    return entry->key;
}

const char* ResolveImeCandidateInsertText(const char* candidate,
                                          char* out_kana,
                                          int out_kana_len,
                                          bool (*is_ascii_romaji_token)(const char*),
                                          int (*convert_romaji_string_to_half_kana)(
                                              const char*, char*, int)) {
    if (candidate == nullptr || candidate[0] == '\0') {
        return candidate;
    }
    if (out_kana == nullptr || out_kana_len <= 0 ||
        is_ascii_romaji_token == nullptr || convert_romaji_string_to_half_kana == nullptr) {
        return candidate;
    }
    if (!is_ascii_romaji_token(candidate)) {
        return candidate;
    }
    if (convert_romaji_string_to_half_kana(candidate, out_kana, out_kana_len) > 0) {
        return out_kana;
    }
    return candidate;
}

}  // namespace input
