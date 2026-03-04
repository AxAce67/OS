#pragma once

#include <stdint.h>

#include "input/key_handler.hpp"
#include "input/key_handler_exec.hpp"

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

}  // namespace input
