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

ExecChainResult ExecuteRegularExecChain(const RegularExecPlan& plan,
                                        const RegularImeActionContext& ime_context,
                                        const RegularClearContext& clear_context,
                                        const RegularModeContext& mode_context,
                                        const RegularActionContext& action_context,
                                        int* cursor_pos,
                                        int command_len) {
    const auto ime_exec_result =
        ExecuteRegularImeActionWithContext(plan.kind, ime_context, cursor_pos);
    if (ime_exec_result == RegularImeExecResult::kFailed) {
        return ExecChainResult::kFailed;
    }
    if (ime_exec_result == RegularImeExecResult::kHandledNeedsRender) {
        return ExecChainResult::kHandledNeedsRender;
    }
    if (ExecuteRegularClearActionWithContext(plan.kind, clear_context)) {
        return ExecChainResult::kHandledNeedsRender;
    }
    if (ExecuteRegularModeActionWithContext(plan.kind, mode_context)) {
        return ExecChainResult::kHandled;
    }
    if (ExecuteRegularActionWithContext(plan.kind, action_context)) {
        return ExecChainResult::kHandled;
    }
    if (ExecuteRegularNeutralAction(plan.kind, cursor_pos, command_len)) {
        return ExecChainResult::kHandledNeedsRender;
    }
    return ExecChainResult::kNotHandled;
}

ExecChainResult ExecuteExtendedExecChain(const ExtendedExecPlan& plan,
                                         const ExtendedActionContext& action_context,
                                         int* cursor_pos,
                                         int command_len) {
    if (ExecuteExtendedActionWithContext(plan.kind, action_context)) {
        return ExecChainResult::kHandled;
    }
    const auto cursor_move =
        ExecuteExtendedCursorMoveAction(plan.kind, cursor_pos, command_len);
    if (!cursor_move.handled) {
        return ExecChainResult::kNotHandled;
    }
    if (cursor_move.should_render) {
        return ExecChainResult::kHandledNeedsRender;
    }
    return ExecChainResult::kHandled;
}

}  // namespace input
