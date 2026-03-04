#pragma once

#include <stdint.h>

namespace input {

int SelectBestCandidateIndex(const char* default_key,
                             const char* const* candidates,
                             const char source_keys[][32],
                             int count,
                             uint16_t (*get_score)(const char* key, const char* cand));

int WrapCandidateIndex(int current, int delta, int count);

void SortCandidatesByLearning(const char** candidates,
                              char source_keys[][32],
                              int count,
                              uint16_t (*get_score)(const char* key, const char* cand));

}  // namespace input

