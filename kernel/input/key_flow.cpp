#include "input/key_flow.hpp"

namespace input {

CandidateNav DecideCandidateNavOnExtendedKey(uint8_t key,
                                             bool candidate_active,
                                             const ImeCandidateEntry* entry) {
    if (!candidate_active || entry == nullptr || entry->count <= 0) {
        return CandidateNav::kNone;
    }
    if (key == 0x48) {  // Arrow Up
        return CandidateNav::kPrev;
    }
    if (key == 0x50) {  // Arrow Down
        return CandidateNav::kNext;
    }
    return CandidateNav::kNone;
}

}  // namespace input
