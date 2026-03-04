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

bool ShouldFlushRomajiBeforeExtendedKey(bool ime_enabled,
                                        int romaji_len,
                                        CandidateNav nav) {
    if (nav != CandidateNav::kNone) {
        return false;
    }
    return ime_enabled && romaji_len > 0;
}

bool ShouldClearCandidateBeforeExtendedKey(bool candidate_active,
                                           CandidateNav nav) {
    if (nav != CandidateNav::kNone) {
        return false;
    }
    return candidate_active;
}

bool ShouldFlushRomajiForCursorShortcut(bool ime_enabled, int romaji_len) {
    return ime_enabled && romaji_len > 0;
}

}  // namespace input

