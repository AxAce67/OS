#include "input/ime_logic.hpp"

namespace input {

int SelectBestCandidateIndex(const char* default_key,
                             const char* const* candidates,
                             const char source_keys[][32],
                             int count,
                             uint16_t (*get_score)(const char* key, const char* cand)) {
    if (candidates == nullptr || get_score == nullptr || count <= 0) {
        return 0;
    }
    int best_idx = 0;
    uint16_t best_score = 0;
    for (int i = 0; i < count; ++i) {
        const char* cand = candidates[i];
        if (cand == nullptr || cand[0] == '\0') {
            continue;
        }
        const char* key = (source_keys != nullptr && source_keys[i][0] != '\0')
                            ? source_keys[i]
                            : default_key;
        const uint16_t score = get_score(key, cand);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return best_idx;
}

int WrapCandidateIndex(int current, int delta, int count) {
    if (count <= 0) {
        return 0;
    }
    int next = current + delta;
    while (next < 0) {
        next += count;
    }
    while (next >= count) {
        next -= count;
    }
    return next;
}

void SortCandidatesByLearning(const char** candidates,
                              char source_keys[][32],
                              int count,
                              uint16_t (*get_score)(const char* key, const char* cand)) {
    if (candidates == nullptr || source_keys == nullptr || get_score == nullptr || count <= 1) {
        return;
    }
    for (int i = 0; i + 1 < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            const uint16_t score_i = get_score(source_keys[i], candidates[i]);
            const uint16_t score_j = get_score(source_keys[j], candidates[j]);
            if (score_j <= score_i) {
                continue;
            }
            const char* tmp_ptr = candidates[i];
            candidates[i] = candidates[j];
            candidates[j] = tmp_ptr;
            char tmp_key[32];
            for (int k = 0; k < 32; ++k) {
                tmp_key[k] = source_keys[i][k];
                source_keys[i][k] = source_keys[j][k];
                source_keys[j][k] = tmp_key[k];
            }
        }
    }
}

}  // namespace input

