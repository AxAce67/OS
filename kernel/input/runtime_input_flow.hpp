#pragma once

#include <stdint.h>

#include "input/history.hpp"
#include "input/key_handler.hpp"
#include "input/key_handler_exec.hpp"
#include "input/key_event.hpp"
#include "input/key_flow.hpp"

class Console;

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

enum class PlanPrepareStatus {
    kNotReady = 0,
    kReady,
};

inline bool ExecChainNeedsRender(ExecChainResult result) {
    return result == ExecChainResult::kHandledNeedsRender;
}

inline bool ExecChainHandled(ExecChainResult result) {
    return result == ExecChainResult::kHandled ||
           result == ExecChainResult::kHandledNeedsRender;
}

template <class TAcquireBundles, class TOnRender, class TComputeChain>
inline bool HandleRuntimeExecChain(TAcquireBundles&& acquire_bundles,
                                   TOnRender&& on_render,
                                   TComputeChain&& compute_chain_result) {
    auto bundles = acquire_bundles();
    const auto chain_result = compute_chain_result(&bundles);
    if (ExecChainNeedsRender(chain_result)) {
        on_render();
    }
    return ExecChainHandled(chain_result);
}

template <class TRefresh,
          class TCycle, class TBrowseUp, class TBrowseDown,
          class TBackspace, class TDelete, class TTab>
struct RuntimeInputActionOwnerT {
    Console* console;
    TRefresh* refresh_console;
    TCycle* cycle_candidate;
    TBrowseUp* browse_history_up;
    TBrowseDown* browse_history_down;
    TBackspace* backspace_at_cursor;
    TDelete* delete_at_cursor;
    TTab* tab_complete;
};

template <class TDeleteRange,
          class TClearCandidate,
          class TPrintPrompt,
          class TClearSelection,
          class THistory,
          class TRepaintPromptAndInput>
struct RuntimeRegularShortcutOwnerT {
    TDeleteRange* delete_range_at;
    TClearCandidate* clear_ime_candidate;
    Console* console;
    int* input_row;
    int* input_col;
    TPrintPrompt* print_prompt;
    TClearSelection* clear_selection;
    THistory* command_history;
    TRepaintPromptAndInput* repaint_prompt_and_input;
};

template <class TInputActionOwner>
struct RuntimeExtendedExecBundleT {
    TInputActionOwner owner;
};

template <class TInputActionOwner, class TRegularShortcutOwner>
struct RuntimeRegularExecBundleT {
    TInputActionOwner action_owner;
    TRegularShortcutOwner shortcut_owner;
};

template <class TExtendedExecBundle, class TRegularExecBundle>
struct RuntimeFlowBundlesT {
    TExtendedExecBundle extended;
    TRegularExecBundle regular;
};

template <class TImeCandidateEntry>
struct RuntimeExecInputRefsT {
    const TImeCandidateEntry* ime_candidate_entry;
    int ime_candidate_start;
    int ime_candidate_len;
    char* ime_romaji_buffer;
    int ime_romaji_capacity;
    int* ime_romaji_len;
    char* command_buffer;
    int command_capacity;
    int* command_len;
    int* cursor_pos;
    int* rendered_len;
    bool* ime_enabled;
    bool* jp_layout;
    int (*str_length)(const char*);
};

struct RuntimeKeyDownRefs {
    bool* key_down_extended;
    bool* key_down_normal;
};

inline bool TryConsumeReleasedKey(const KeyEvent& key_event,
                                  const RuntimeKeyDownRefs& refs) {
    if (!key_event.released) {
        return false;
    }
    if (key_event.keycode < 128) {
        if (key_event.extended) {
            refs.key_down_extended[key_event.keycode] = false;
        } else {
            refs.key_down_normal[key_event.keycode] = false;
        }
    }
    return true;
}

inline bool ShouldSkipRepeatedKeyDown(const KeyEvent& key_event,
                                      bool key_repeat_enabled,
                                      const RuntimeKeyDownRefs& refs) {
    if (key_repeat_enabled || key_event.keycode >= 128) {
        return false;
    }
    const bool already_down = key_event.extended
                                  ? refs.key_down_extended[key_event.keycode]
                                  : refs.key_down_normal[key_event.keycode];
    return already_down;
}

inline void MarkKeyDownIfTrackable(const KeyEvent& key_event,
                                   const RuntimeKeyDownRefs& refs) {
    if (key_event.keycode >= 128) {
        return;
    }
    if (key_event.extended) {
        refs.key_down_extended[key_event.keycode] = true;
    } else {
        refs.key_down_normal[key_event.keycode] = true;
    }
}

template <class THandleExtendedKey>
inline bool ShouldProcessAfterExtendedKey(const KeyEvent& key_event,
                                          uint8_t key,
                                          THandleExtendedKey&& handle_extended_key) {
    if (!key_event.extended) {
        return true;
    }
    if (handle_extended_key(key)) {
        return false;
    }
    // Extended keypad Enter and keypad Slash should behave as text input keys.
    return key == 0x1C || key == 0x35;
}

template <class TRefreshConsole, class TRefreshInputLine>
inline void RefreshAfterKeyboardCharInput(bool full_refresh,
                                          TRefreshConsole&& refresh_console,
                                          TRefreshInputLine&& refresh_input_line) {
    if (full_refresh) {
        refresh_console();
        return;
    }
    refresh_input_line();
}

template <class TImeCandidateEntry>
inline void BuildRuntimeExecInputRefs(RuntimeExecInputRefsT<TImeCandidateEntry>* refs,
                                      const TImeCandidateEntry* ime_candidate_entry,
                                      int ime_candidate_start,
                                      int ime_candidate_len,
                                      char* ime_romaji_buffer,
                                      int ime_romaji_capacity,
                                      int* ime_romaji_len,
                                      char* command_buffer,
                                      int command_capacity,
                                      int* command_len,
                                      int* cursor_pos,
                                      int* rendered_len,
                                      bool* ime_enabled,
                                      bool* jp_layout,
                                      int (*str_length)(const char*)) {
    if (refs == nullptr) {
        return;
    }
    refs->ime_candidate_entry = ime_candidate_entry;
    refs->ime_candidate_start = ime_candidate_start;
    refs->ime_candidate_len = ime_candidate_len;
    refs->ime_romaji_buffer = ime_romaji_buffer;
    refs->ime_romaji_capacity = ime_romaji_capacity;
    refs->ime_romaji_len = ime_romaji_len;
    refs->command_buffer = command_buffer;
    refs->command_capacity = command_capacity;
    refs->command_len = command_len;
    refs->cursor_pos = cursor_pos;
    refs->rendered_len = rendered_len;
    refs->ime_enabled = ime_enabled;
    refs->jp_layout = jp_layout;
    refs->str_length = str_length;
}

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

template <class TFlushRomaji, class TClearCandidate, class TEnsureLiveConsole, class TClearSelection>
inline void ApplyExtendedPlanSideEffects(const ExtendedExecPlan& plan,
                                         TFlushRomaji&& flush_romaji,
                                         TClearCandidate&& clear_candidate,
                                         TEnsureLiveConsole&& ensure_live_console,
                                         TClearSelection&& clear_selection) {
    if (plan.flush_romaji) {
        flush_romaji();
    }
    if (plan.clear_candidate) {
        clear_candidate();
    }
    if (plan.ensure_live_console) {
        ensure_live_console();
    }
    if (plan.clear_selection) {
        clear_selection();
    }
}

template <class TFlushRomaji, class TEnsureLiveConsole, class TClearSelection>
inline void ApplyRegularPlanSideEffects(const RegularExecPlan& plan,
                                        TFlushRomaji&& flush_romaji,
                                        TEnsureLiveConsole&& ensure_live_console,
                                        TClearSelection&& clear_selection) {
    if (plan.flush_romaji) {
        flush_romaji();
    }
    if (plan.ensure_live_console) {
        ensure_live_console();
    }
    if (plan.clear_selection) {
        clear_selection();
    }
}

template <class TApplySideEffects>
inline bool TryPrepareExtendedExecPlan(uint8_t key,
                                       bool ime_enabled,
                                       int ime_romaji_len,
                                       bool candidate_active,
                                       bool has_candidate_nav,
                                       TApplySideEffects&& apply_side_effects,
                                       ExtendedExecPlan* out_plan) {
    if (out_plan == nullptr) {
        return false;
    }
    const auto prep = PrepareExtendedExecPlan(key,
                                              ime_enabled,
                                              ime_romaji_len,
                                              candidate_active,
                                              has_candidate_nav);
    if (!prep.handled) {
        return false;
    }
    apply_side_effects(prep.plan);
    *out_plan = prep.plan;
    return true;
}

template <class TApplySideEffects>
inline PlanPrepareStatus PrepareExtendedExecPlanStatus(uint8_t key,
                                                       bool ime_enabled,
                                                       int ime_romaji_len,
                                                       bool candidate_active,
                                                       bool has_candidate_nav,
                                                       TApplySideEffects&& apply_side_effects,
                                                       ExtendedExecPlan* out_plan) {
    return TryPrepareExtendedExecPlan(key,
                                      ime_enabled,
                                      ime_romaji_len,
                                      candidate_active,
                                      has_candidate_nav,
                                      apply_side_effects,
                                      out_plan)
               ? PlanPrepareStatus::kReady
               : PlanPrepareStatus::kNotReady;
}

template <class TEnsureLiveConsole, class TCycleCandidate>
inline bool TryHandleCandidateNav(CandidateNav nav,
                                  TEnsureLiveConsole&& ensure_live_console,
                                  TCycleCandidate&& cycle_candidate) {
    if (nav == CandidateNav::kPrev) {
        ensure_live_console();
        return cycle_candidate(-1);
    }
    if (nav == CandidateNav::kNext) {
        ensure_live_console();
        return cycle_candidate(1);
    }
    return false;
}

template <class TAdvanceCandidate, class TReplaceCandidateText>
inline bool TryHandleImeCandidateCycle(bool cycle_candidate,
                                       TAdvanceCandidate&& advance_candidate,
                                       TReplaceCandidateText&& replace_candidate_text) {
    if (!cycle_candidate) {
        return false;
    }
    if (!advance_candidate()) {
        return false;
    }
    replace_candidate_text();
    return true;
}

template <class TCommitLearning, class TClearCandidate>
inline void ApplyImeCommitSideEffects(bool commit_candidate,
                                      TCommitLearning&& commit_learning,
                                      TClearCandidate&& clear_candidate) {
    if (!commit_candidate) {
        return;
    }
    commit_learning();
    clear_candidate();
}

template <class TFlushRomaji, class TRenderInputLine, class TRefreshInputLine>
inline bool TryHandleImeAppendAlpha(bool append_alpha,
                                    char lower_alpha,
                                    char* romaji_buffer,
                                    int romaji_capacity,
                                    int* romaji_len,
                                    TFlushRomaji&& flush_romaji,
                                    TRenderInputLine&& render_input_line,
                                    TRefreshInputLine&& refresh_input_line) {
    if (!append_alpha) {
        return false;
    }
    if (romaji_buffer == nullptr || romaji_len == nullptr || romaji_capacity <= 1) {
        return true;
    }
    if (*romaji_len + 1 < romaji_capacity) {
        romaji_buffer[(*romaji_len)++] = lower_alpha;
        romaji_buffer[*romaji_len] = '\0';
        if (!flush_romaji(false)) {
            render_input_line();
            refresh_input_line();
        }
    }
    return true;
}

template <class TOnCommitActiveCandidate, class TApplySideEffects>
inline bool TryPrepareRegularExecPlan(uint8_t key,
                                      bool ctrl_pressed,
                                      bool num_lock,
                                      bool ime_enabled,
                                      int ime_romaji_len,
                                      bool candidate_active,
                                      bool has_candidate_entry,
                                      TOnCommitActiveCandidate&& on_commit_active_candidate,
                                      TApplySideEffects&& apply_side_effects,
                                      RegularExecPlan* out_plan) {
    if (out_plan == nullptr) {
        return false;
    }
    const auto prep = PrepareRegularExecPlan(key,
                                             ctrl_pressed,
                                             num_lock,
                                             ime_enabled,
                                             ime_romaji_len,
                                             candidate_active,
                                             has_candidate_entry);
    if (prep.should_commit_active_candidate) {
        on_commit_active_candidate();
    }
    if (!prep.handled || prep.missing_required_candidate) {
        return false;
    }
    apply_side_effects(prep.plan);
    *out_plan = prep.plan;
    return true;
}

template <class TOnCommitActiveCandidate, class TApplySideEffects>
inline PlanPrepareStatus PrepareRegularExecPlanStatus(uint8_t key,
                                                      bool ctrl_pressed,
                                                      bool num_lock,
                                                      bool ime_enabled,
                                                      int ime_romaji_len,
                                                      bool candidate_active,
                                                      bool has_candidate_entry,
                                                      TOnCommitActiveCandidate&& on_commit_active_candidate,
                                                      TApplySideEffects&& apply_side_effects,
                                                      RegularExecPlan* out_plan) {
    return TryPrepareRegularExecPlan(key,
                                     ctrl_pressed,
                                     num_lock,
                                     ime_enabled,
                                     ime_romaji_len,
                                     candidate_active,
                                     has_candidate_entry,
                                     on_commit_active_candidate,
                                     apply_side_effects,
                                     out_plan)
               ? PlanPrepareStatus::kReady
               : PlanPrepareStatus::kNotReady;
}

template <class TInputActionOwner>
inline ExtendedActionContext BuildExtendedActionContext(TInputActionOwner* owner) {
    return ExtendedActionContext{
        owner,
        [](void* ctx_owner, int lines) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            o->console->ScrollUp(lines);
            (*o->refresh_console)();
        },
        [](void* ctx_owner, int lines) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            o->console->ScrollDown(lines);
            (*o->refresh_console)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->delete_at_cursor)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->browse_history_up)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->browse_history_down)();
        },
    };
}

template <class TExtendedExecBundle>
inline ExecChainResult ExecuteExtendedExecChainWithOwner(TExtendedExecBundle* bundle,
                                                         const ExtendedExecPlan& plan,
                                                         int* cursor_pos,
                                                         int command_len) {
    const auto action_context = BuildExtendedActionContext(&bundle->owner);
    return ExecuteExtendedExecChain(plan, action_context, cursor_pos, command_len);
}

template <class TInputActionOwner>
inline RegularActionContext BuildRegularActionContext(TInputActionOwner* owner) {
    return RegularActionContext{
        owner,
        [](void* ctx_owner, int direction) -> bool {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            return (*o->cycle_candidate)(direction);
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->browse_history_up)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->browse_history_down)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->backspace_at_cursor)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->delete_at_cursor)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TInputActionOwner*>(ctx_owner);
            (*o->tab_complete)();
        },
    };
}

template <class TRegularShortcutOwner>
inline RegularImeActionContext BuildRegularImeContext(TRegularShortcutOwner* owner,
                                                      const ImeCandidateEntry* candidate_entry,
                                                      int candidate_start,
                                                      int candidate_len,
                                                      char* romaji_buffer,
                                                      int romaji_capacity,
                                                      int* romaji_len,
                                                      int (*str_length)(const char*)) {
    return RegularImeActionContext{
        owner,
        candidate_entry,
        candidate_start,
        candidate_len,
        romaji_buffer,
        romaji_capacity,
        romaji_len,
        str_length,
        [](void* ctx_owner, int start, int len) -> bool {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            return (*o->delete_range_at)(start, len);
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            (*o->clear_ime_candidate)();
        },
    };
}

template <class TRegularShortcutOwner>
inline RegularClearContext BuildRegularClearContext(TRegularShortcutOwner* owner,
                                                    char* command_buffer,
                                                    int command_capacity,
                                                    int* command_len,
                                                    int* cursor_pos,
                                                    int* rendered_len,
                                                    char* romaji_buffer,
                                                    int romaji_capacity,
                                                    int* romaji_len) {
    return RegularClearContext{
        owner,
        command_buffer,
        command_capacity,
        command_len,
        cursor_pos,
        rendered_len,
        romaji_buffer,
        romaji_capacity,
        romaji_len,
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            o->console->Clear();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            (*o->print_prompt)();
            *o->input_row = o->console->CursorRow();
            *o->input_col = o->console->CursorColumn();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            (*o->clear_ime_candidate)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            (*o->clear_selection)();
        },
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            o->command_history->ResetNavigation();
        },
    };
}

template <class TRegularShortcutOwner>
inline RegularModeContext BuildRegularModeContext(TRegularShortcutOwner* owner,
                                                  RegularShortcutAction mode_action,
                                                  bool* ime_enabled,
                                                  bool* jp_layout) {
    return RegularModeContext{
        owner,
        mode_action,
        ime_enabled,
        jp_layout,
        [](void* ctx_owner) {
            auto* o = reinterpret_cast<TRegularShortcutOwner*>(ctx_owner);
            (*o->repaint_prompt_and_input)();
        },
    };
}

template <class TRegularExecBundle, class TImeCandidateEntry>
inline ExecChainResult ExecuteRegularExecChainWithRefs(TRegularExecBundle* bundle,
                                                       const RegularExecPlan& plan,
                                                       const RuntimeExecInputRefsT<TImeCandidateEntry>& refs) {
    const auto action_context = BuildRegularActionContext(&bundle->action_owner);
    const auto ime_context = BuildRegularImeContext(&bundle->shortcut_owner,
                                                    refs.ime_candidate_entry,
                                                    refs.ime_candidate_start,
                                                    refs.ime_candidate_len,
                                                    refs.ime_romaji_buffer,
                                                    refs.ime_romaji_capacity,
                                                    refs.ime_romaji_len,
                                                    refs.str_length);
    const auto clear_context = BuildRegularClearContext(&bundle->shortcut_owner,
                                                        refs.command_buffer,
                                                        refs.command_capacity,
                                                        refs.command_len,
                                                        refs.cursor_pos,
                                                        refs.rendered_len,
                                                        refs.ime_romaji_buffer,
                                                        refs.ime_romaji_capacity,
                                                        refs.ime_romaji_len);
    const auto mode_context = BuildRegularModeContext(&bundle->shortcut_owner,
                                                      plan.mode_action,
                                                      refs.ime_enabled,
                                                      refs.jp_layout);
    return ExecuteRegularExecChain(plan,
                                   ime_context,
                                   clear_context,
                                   mode_context,
                                   action_context,
                                   refs.cursor_pos,
                                   *refs.command_len);
}

template <class TExtendedExecBundle, class TRefresh, class TBrowseUp, class TBrowseDown, class TDelete>
inline void BuildExtendedExecBundle(TExtendedExecBundle* bundle,
                                    Console* console,
                                    TRefresh* refresh_console,
                                    TBrowseUp* browse_history_up,
                                    TBrowseDown* browse_history_down,
                                    TDelete* delete_at_cursor) {
    if (bundle == nullptr) {
        return;
    }
    bundle->owner.console = console;
    bundle->owner.refresh_console = refresh_console;
    bundle->owner.cycle_candidate = nullptr;
    bundle->owner.browse_history_up = browse_history_up;
    bundle->owner.browse_history_down = browse_history_down;
    bundle->owner.backspace_at_cursor = nullptr;
    bundle->owner.delete_at_cursor = delete_at_cursor;
    bundle->owner.tab_complete = nullptr;
}

template <class TRegularExecBundle,
          class TRefresh, class TCycle, class TBrowseUp, class TBrowseDown,
          class TBackspace, class TDelete, class TTab,
          class TDeleteRange, class TClearCandidate, class TPrintPrompt,
          class TClearSelection, class THistory, class TRepaintPromptAndInput>
inline void BuildRegularExecBundle(TRegularExecBundle* bundle,
                                   Console* console,
                                   TRefresh* refresh_console,
                                   TCycle* cycle_candidate,
                                   TBrowseUp* browse_history_up,
                                   TBrowseDown* browse_history_down,
                                   TBackspace* backspace_at_cursor,
                                   TDelete* delete_at_cursor,
                                   TTab* tab_complete,
                                   TDeleteRange* delete_range_at,
                                   TClearCandidate* clear_ime_candidate,
                                   int* input_row,
                                   int* input_col,
                                   TPrintPrompt* print_prompt,
                                   TClearSelection* clear_selection,
                                   THistory* command_history,
                                   TRepaintPromptAndInput* repaint_prompt_and_input) {
    if (bundle == nullptr) {
        return;
    }
    bundle->action_owner.console = console;
    bundle->action_owner.refresh_console = refresh_console;
    bundle->action_owner.cycle_candidate = cycle_candidate;
    bundle->action_owner.browse_history_up = browse_history_up;
    bundle->action_owner.browse_history_down = browse_history_down;
    bundle->action_owner.backspace_at_cursor = backspace_at_cursor;
    bundle->action_owner.delete_at_cursor = delete_at_cursor;
    bundle->action_owner.tab_complete = tab_complete;

    bundle->shortcut_owner.delete_range_at = delete_range_at;
    bundle->shortcut_owner.clear_ime_candidate = clear_ime_candidate;
    bundle->shortcut_owner.console = console;
    bundle->shortcut_owner.input_row = input_row;
    bundle->shortcut_owner.input_col = input_col;
    bundle->shortcut_owner.print_prompt = print_prompt;
    bundle->shortcut_owner.clear_selection = clear_selection;
    bundle->shortcut_owner.command_history = command_history;
    bundle->shortcut_owner.repaint_prompt_and_input = repaint_prompt_and_input;
}

template <class TFlowBundles,
          class TRefresh, class TCycle, class TBrowseUp, class TBrowseDown,
          class TBackspace, class TDelete, class TTab,
          class TDeleteRange, class TClearCandidate, class TPrintPrompt,
          class TClearSelection, class THistory, class TRepaintPromptAndInput>
inline void BuildRuntimeFlowBundles(TFlowBundles* bundles,
                                    Console* console,
                                    TRefresh* refresh_console,
                                    TCycle* cycle_candidate,
                                    TBrowseUp* browse_history_up,
                                    TBrowseDown* browse_history_down,
                                    TBackspace* backspace_at_cursor,
                                    TDelete* delete_at_cursor,
                                    TTab* tab_complete,
                                    TDeleteRange* delete_range_at,
                                    TClearCandidate* clear_ime_candidate,
                                    int* input_row,
                                    int* input_col,
                                    TPrintPrompt* print_prompt,
                                    TClearSelection* clear_selection,
                                    THistory* command_history,
                                    TRepaintPromptAndInput* repaint_prompt_and_input) {
    if (bundles == nullptr) {
        return;
    }
    BuildExtendedExecBundle(&bundles->extended,
                            console,
                            refresh_console,
                            browse_history_up,
                            browse_history_down,
                            delete_at_cursor);
    BuildRegularExecBundle(&bundles->regular,
                           console,
                           refresh_console,
                           cycle_candidate,
                           browse_history_up,
                           browse_history_down,
                           backspace_at_cursor,
                           delete_at_cursor,
                           tab_complete,
                           delete_range_at,
                           clear_ime_candidate,
                           input_row,
                           input_col,
                           print_prompt,
                           clear_selection,
                           command_history,
                           repaint_prompt_and_input);
}

}  // namespace input
