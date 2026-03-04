#include "input/runtime_input_flow.hpp"

namespace input {

RegularPlanPrepResult PrepareRegularExecPlan(uint8_t key,
                                             bool ctrl_pressed,
                                             bool num_lock,
                                             bool ime_enabled,
                                             int ime_romaji_len,
                                             bool candidate_active,
                                             bool has_candidate_entry) {
    RegularPlanPrepResult out{};
    out.should_commit_active_candidate =
        ShouldCommitActiveCandidateBeforeKey(candidate_active, key);
    const RegularShortcutAction action =
        DecideRegularShortcutAction(key, ctrl_pressed, num_lock);
    out.plan = BuildRegularExecPlan(action,
                                    ime_enabled,
                                    ime_romaji_len,
                                    candidate_active);
    out.handled = out.plan.handled;
    out.missing_required_candidate =
        out.plan.requires_active_candidate &&
        (!candidate_active || !has_candidate_entry);
    return out;
}

ExtendedPlanPrepResult PrepareExtendedExecPlan(uint8_t key,
                                               bool ime_enabled,
                                               int ime_romaji_len,
                                               bool candidate_active,
                                               bool has_candidate_nav) {
    ExtendedPlanPrepResult out{};
    const ExtendedKeyAction action = DecideExtendedKeyAction(key);
    out.plan = BuildExtendedExecPlan(action,
                                     ime_enabled,
                                     ime_romaji_len,
                                     candidate_active,
                                     has_candidate_nav);
    out.handled = out.plan.handled;
    return out;
}

}  // namespace input

