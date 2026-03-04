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

}  // namespace input

