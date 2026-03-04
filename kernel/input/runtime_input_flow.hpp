#pragma once

#include <stdint.h>

#include "input/history.hpp"
#include "input/ime_session.hpp"
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

struct RuntimeKeyboardDecodeRefs {
    uint64_t* irq_count;
    uint8_t* last_raw;
    uint8_t* last_key;
    bool* last_extended;
    bool* last_released;
    bool* e0_prefix;
    KeyboardModifiers* modifiers;
};

struct RuntimeCommandInputStateRefs {
    char* command_buffer;
    int command_capacity;
    int* command_len;
    int* cursor_pos;
    int* rendered_len;
    char* ime_romaji_buffer;
    int ime_romaji_capacity;
    int* ime_romaji_len;
};

struct RuntimeEnterCommandRefs {
    char* command_buffer;
    int command_capacity;
    int command_len;
};

struct RuntimeCharTranslationResult {
    bool has_char;
    char ch;
};

template <class TCandidateEntry>
struct RuntimeImeCandidateStartRefsT {
    char* romaji_buffer;
    int* romaji_len;
    const TCandidateEntry** active_entry;
};

template <class TCandidateEntry>
struct RuntimeImeProcessContextT {
    char* romaji_buffer;
    int romaji_capacity;
    int* romaji_len;
    const TCandidateEntry** candidate_entry;
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
                                          THandleExtendedKey&& handle_extended_key);

template <class THandleExtendedKey>
inline bool PrepareKeyboardEventForDispatch(const KeyEvent& key_event,
                                            bool key_repeat_enabled,
                                            const RuntimeKeyDownRefs& key_down_refs,
                                            THandleExtendedKey&& handle_extended_key) {
    if (key_event.kind == KeyEventKind::kModifier) {
        return false;
    }
    if (TryConsumeReleasedKey(key_event, key_down_refs)) {
        return false;
    }
    if (!ShouldProcessAfterExtendedKey(key_event, key_event.keycode, handle_extended_key)) {
        return false;
    }
    if (ShouldSkipRepeatedKeyDown(key_event, key_repeat_enabled, key_down_refs)) {
        return false;
    }
    MarkKeyDownIfTrackable(key_event, key_down_refs);
    return true;
}

inline bool DecodeKeyboardMessageAndTrack(uint8_t raw_scancode,
                                          const RuntimeKeyboardDecodeRefs& refs,
                                          KeyEvent* out_key_event) {
    if (out_key_event == nullptr ||
        refs.irq_count == nullptr ||
        refs.last_raw == nullptr ||
        refs.last_key == nullptr ||
        refs.last_extended == nullptr ||
        refs.last_released == nullptr ||
        refs.e0_prefix == nullptr ||
        refs.modifiers == nullptr) {
        return false;
    }
    ++(*refs.irq_count);
    *refs.last_raw = raw_scancode;
    if (!DecodePS2Set1KeyEvent(raw_scancode, refs.e0_prefix, refs.modifiers, out_key_event)) {
        return false;
    }
    *refs.last_key = out_key_event->keycode;
    *refs.last_extended = out_key_event->extended;
    *refs.last_released = out_key_event->released;
    return true;
}

template <class TKeycodeToAscii>
inline RuntimeCharTranslationResult TranslateKeyEventToAscii(const KeyEvent& key_event,
                                                             bool jp_layout,
                                                             TKeycodeToAscii&& keycode_to_ascii) {
    const char ch = keycode_to_ascii(key_event.keycode,
                                     key_event.shift,
                                     key_event.caps_lock,
                                     key_event.num_lock,
                                     jp_layout);
    return RuntimeCharTranslationResult{ch != 0, ch};
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

template <class TFlushRomaji>
inline void FinalizeImeRomajiIfNeeded(bool finalize_romaji,
                                      TFlushRomaji&& flush_romaji) {
    if (!finalize_romaji) {
        return;
    }
    // Finalize pending romaji before non-alpha key (space/punct/enter).
    flush_romaji(true);
}

template <class TStrEqual, class TPrintHistory, class TClearHistory, class TExecuteCommand>
inline void ExecuteShellCommandOrHistory(const char* command,
                                         TStrEqual&& str_equal,
                                         TPrintHistory&& print_history,
                                         TClearHistory&& clear_history,
                                         TExecuteCommand&& execute_command) {
    if (str_equal(command, "history")) {
        print_history();
        return;
    }
    if (str_equal(command, "clearhistory")) {
        clear_history();
        return;
    }
    execute_command();
}

template <class TIsPrintable, class TOnEnter, class TOnPrintable>
inline bool ProcessKeyboardCharAction(char ch,
                                      TIsPrintable&& is_printable,
                                      TOnEnter&& on_enter,
                                      TOnPrintable&& on_printable) {
    if (ch == '\n') {
        on_enter();
        return true;
    }
    if (is_printable(ch)) {
        on_printable(static_cast<uint8_t>(ch));
    }
    return false;
}

template <class TSetInputCursorToLineEnd,
          class TPrintNewLine,
          class TAddHistory,
          class TStrEqual,
          class TPrintHistory,
          class TClearHistory,
          class TExecuteCommand,
          class TClearCandidate,
          class TClearSelection,
          class TResetHistory,
          class TPrintPrompt,
          class TPlacePromptCursor>
inline void ProcessEnterCommandAction(const RuntimeEnterCommandRefs& enter_refs,
                                      const RuntimeCommandInputStateRefs& reset_refs,
                                      TSetInputCursorToLineEnd&& set_input_cursor_to_line_end,
                                      TPrintNewLine&& print_new_line,
                                      TAddHistory&& add_history,
                                      TStrEqual&& str_equal,
                                      TPrintHistory&& print_history,
                                      TClearHistory&& clear_history,
                                      TExecuteCommand&& execute_command,
                                      TClearCandidate&& clear_candidate,
                                      TClearSelection&& clear_selection,
                                      TResetHistory&& reset_history,
                                      TPrintPrompt&& print_prompt,
                                      TPlacePromptCursor&& place_prompt_cursor) {
    if (enter_refs.command_buffer == nullptr || enter_refs.command_capacity <= 0) {
        return;
    }
    if (enter_refs.command_len < 0 || enter_refs.command_len >= enter_refs.command_capacity) {
        return;
    }

    set_input_cursor_to_line_end();
    print_new_line();
    enter_refs.command_buffer[enter_refs.command_len] = '\0';
    if (enter_refs.command_len > 0) {
        add_history(enter_refs.command_buffer);
    }
    ExecuteShellCommandOrHistory(enter_refs.command_buffer,
                                 str_equal,
                                 print_history,
                                 clear_history,
                                 execute_command);
    ResetAfterCommandExecution(reset_refs,
                               clear_candidate,
                               clear_selection,
                               reset_history,
                               print_prompt,
                               place_prompt_cursor);
}

template <class TClearCandidate,
          class TClearSelection,
          class TResetHistory,
          class TPrintPrompt,
          class TPlacePromptCursor>
inline void ResetAfterCommandExecution(const RuntimeCommandInputStateRefs& refs,
                                       TClearCandidate&& clear_candidate,
                                       TClearSelection&& clear_selection,
                                       TResetHistory&& reset_history,
                                       TPrintPrompt&& print_prompt,
                                       TPlacePromptCursor&& place_prompt_cursor) {
    if (refs.command_len != nullptr) {
        *refs.command_len = 0;
    }
    if (refs.cursor_pos != nullptr) {
        *refs.cursor_pos = 0;
    }
    if (refs.rendered_len != nullptr) {
        *refs.rendered_len = 0;
    }
    if (refs.command_buffer != nullptr && refs.command_capacity > 0) {
        refs.command_buffer[0] = '\0';
    }
    if (refs.ime_romaji_len != nullptr) {
        *refs.ime_romaji_len = 0;
    }
    if (refs.ime_romaji_buffer != nullptr && refs.ime_romaji_capacity > 0) {
        refs.ime_romaji_buffer[0] = '\0';
    }

    clear_candidate();
    clear_selection();
    reset_history();
    print_prompt();
    place_prompt_cursor();
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

template <class TCandidateEntry,
          class TResolveEntry,
          class TBuildPrefixEntry,
          class TIsEntryUsable,
          class THasSelection,
          class TDeleteSelection,
          class TStartSession,
          class TReplaceCandidateText>
inline bool TryStartImeCandidateFromRomaji(bool try_start_candidate,
                                           const RuntimeImeCandidateStartRefsT<TCandidateEntry>& refs,
                                           TResolveEntry&& resolve_entry,
                                           TBuildPrefixEntry&& build_prefix_entry,
                                           TIsEntryUsable&& is_entry_usable,
                                           THasSelection&& has_selection,
                                           TDeleteSelection&& delete_selection,
                                           TStartSession&& start_session,
                                           TReplaceCandidateText&& replace_candidate_text) {
    if (!try_start_candidate) {
        return false;
    }
    if (refs.romaji_buffer == nullptr || refs.romaji_len == nullptr || refs.active_entry == nullptr) {
        return false;
    }

    char keybuf[32];
    const TCandidateEntry* entry = resolve_entry(refs.romaji_buffer, *refs.romaji_len, keybuf, static_cast<int>(sizeof(keybuf)));
    if (entry == nullptr) {
        entry = build_prefix_entry(keybuf);
    }
    if (!is_entry_usable(entry)) {
        return false;
    }

    if (has_selection()) {
        delete_selection();
    }
    *refs.active_entry = entry;
    start_session(entry);
    *refs.romaji_len = 0;
    refs.romaji_buffer[0] = '\0';
    replace_candidate_text();
    return true;
}

template <class TCandidateEntry,
          class TAdvanceCandidate,
          class TReplaceCandidateText,
          class TCommitLearning,
          class TClearCandidate,
          class TFlushRomaji,
          class TRenderInputLine,
          class TRefreshInputLine,
          class TResolveEntry,
          class TBuildPrefixEntry,
          class TIsEntryUsable,
          class THasSelection,
          class TDeleteSelection,
          class TStartSession>
inline bool ProcessImeDecisionPath(const ImeCharDecision& ime_decision,
                                   const RuntimeImeProcessContextT<TCandidateEntry>& context,
                                   TAdvanceCandidate&& advance_candidate,
                                   TReplaceCandidateText&& replace_candidate_text,
                                   TCommitLearning&& commit_learning,
                                   TClearCandidate&& clear_candidate,
                                   TFlushRomaji&& flush_romaji,
                                   TRenderInputLine&& render_input_line,
                                   TRefreshInputLine&& refresh_input_line,
                                   TResolveEntry&& resolve_entry,
                                   TBuildPrefixEntry&& build_prefix_entry,
                                   TIsEntryUsable&& is_entry_usable,
                                   THasSelection&& has_selection,
                                   TDeleteSelection&& delete_selection,
                                   TStartSession&& start_session) {
    if (!ime_decision.ime_path) {
        return false;
    }
    if (TryHandleImeCandidateCycle(ime_decision.cycle_candidate,
                                   advance_candidate,
                                   replace_candidate_text)) {
        return true;
    }
    ApplyImeCommitSideEffects(ime_decision.commit_candidate,
                              commit_learning,
                              clear_candidate);
    if (TryHandleImeAppendAlpha(ime_decision.append_alpha,
                                ime_decision.lower_alpha,
                                context.romaji_buffer,
                                context.romaji_capacity,
                                context.romaji_len,
                                flush_romaji,
                                render_input_line,
                                refresh_input_line)) {
        return true;
    }
    if (TryStartImeCandidateFromRomaji(ime_decision.try_start_candidate,
                                       RuntimeImeCandidateStartRefsT<TCandidateEntry>{
                                           context.romaji_buffer,
                                           context.romaji_len,
                                           context.candidate_entry,
                                       },
                                       resolve_entry,
                                       build_prefix_entry,
                                       is_entry_usable,
                                       has_selection,
                                       delete_selection,
                                       start_session,
                                       replace_candidate_text)) {
        return true;
    }
    FinalizeImeRomajiIfNeeded(ime_decision.finalize_romaji, flush_romaji);
    return false;
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
