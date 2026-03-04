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

}  // namespace input
