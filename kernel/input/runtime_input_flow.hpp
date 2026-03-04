#pragma once

#include <stdint.h>

#include "input/key_handler.hpp"
#include "input/key_handler_exec.hpp"

namespace input {

struct RegularPlanPrepResult {
    bool handled;
    bool should_commit_active_candidate;
    bool missing_required_candidate;
    RegularExecPlan plan;
};

struct ExtendedPlanPrepResult {
    bool handled;
    ExtendedExecPlan plan;
};

RegularPlanPrepResult PrepareRegularExecPlan(uint8_t key,
                                             bool ctrl_pressed,
                                             bool num_lock,
                                             bool ime_enabled,
                                             int ime_romaji_len,
                                             bool candidate_active,
                                             bool has_candidate_entry);

ExtendedPlanPrepResult PrepareExtendedExecPlan(uint8_t key,
                                               bool ime_enabled,
                                               int ime_romaji_len,
                                               bool candidate_active,
                                               bool has_candidate_nav);

enum class ExecChainResult {
    kNotHandled = 0,
    kHandled,
    kHandledNeedsRender,
    kFailed,
};

ExecChainResult ExecuteRegularExecChain(const RegularExecPlan& plan,
                                        const RegularImeActionContext& ime_context,
                                        const RegularClearContext& clear_context,
                                        const RegularModeContext& mode_context,
                                        const RegularActionContext& action_context,
                                        int* cursor_pos,
                                        int command_len);

ExecChainResult ExecuteExtendedExecChain(const ExtendedExecPlan& plan,
                                         const ExtendedActionContext& action_context,
                                         int* cursor_pos,
                                         int command_len);

}  // namespace input
