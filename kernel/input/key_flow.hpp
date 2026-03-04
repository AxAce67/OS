#pragma once

#include <stdint.h>

#include "input/ime_candidate.hpp"

namespace input {

enum class CandidateNav {
    kNone = 0,
    kPrev,
    kNext,
};

CandidateNav DecideCandidateNavOnExtendedKey(uint8_t key,
                                             bool candidate_active,
                                             const ImeCandidateEntry* entry);

bool ShouldFlushRomajiBeforeExtendedKey(bool ime_enabled,
                                        int romaji_len,
                                        CandidateNav nav);

bool ShouldClearCandidateBeforeExtendedKey(bool candidate_active,
                                           CandidateNav nav);

bool ShouldFlushRomajiForCursorShortcut(bool ime_enabled, int romaji_len);

}  // namespace input

