#pragma once

#include "input/runtime_input_flow.hpp"

namespace input::dispatch {

template <class TApplyWindowFocus,
          class TClearSelection,
          class TEnsureLiveConsole,
          class TRenderInputLine,
          class TRefreshInputLine,
          class TScrollUp,
          class TScrollDown,
          class TRefreshConsole>
struct MouseCallbacksRefT {
    TApplyWindowFocus& apply_window_focus;
    TClearSelection& clear_selection;
    TEnsureLiveConsole& ensure_live_console;
    TRenderInputLine& render_input_line;
    TRefreshInputLine& refresh_input_line;
    TScrollUp& scroll_up;
    TScrollDown& scroll_down;
    TRefreshConsole& refresh_console;

    MouseCallbacksRefT(TApplyWindowFocus& apply_window_focus,
                       TClearSelection& clear_selection,
                       TEnsureLiveConsole& ensure_live_console,
                       TRenderInputLine& render_input_line,
                       TRefreshInputLine& refresh_input_line,
                       TScrollUp& scroll_up,
                       TScrollDown& scroll_down,
                       TRefreshConsole& refresh_console)
        : apply_window_focus{apply_window_focus},
          clear_selection{clear_selection},
          ensure_live_console{ensure_live_console},
          render_input_line{render_input_line},
          refresh_input_line{refresh_input_line},
          scroll_up{scroll_up},
          scroll_down{scroll_down},
          refresh_console{refresh_console} {}
};

template <class TApplyWindowFocus,
          class TClearSelection,
          class TEnsureLiveConsole,
          class TRenderInputLine,
          class TRefreshInputLine,
          class TScrollUp,
          class TScrollDown,
          class TRefreshConsole>
inline void HandleMouseRuntimeWithCallbacks(
    const Message& msg,
    uint64_t now_tick,
    int active_window,
    const RuntimeMouseMessageContext& context,
    const RuntimeMouseWindowGeometry& geometry,
    const RuntimeConsoleGridMetrics& console_metrics,
    const RuntimeMouseConsoleSelectionRefs& selection_refs,
    bool has_selection,
    const MouseCallbacksRefT<TApplyWindowFocus,
                             TClearSelection,
                             TEnsureLiveConsole,
                             TRenderInputLine,
                             TRefreshInputLine,
                             TScrollUp,
                             TScrollDown,
                             TRefreshConsole>& callbacks) {
    HandleMouseMessageRuntime(
        msg,
        now_tick,
        active_window,
        context,
        geometry,
        console_metrics,
        selection_refs,
        has_selection,
        callbacks.apply_window_focus,
        callbacks.clear_selection,
        callbacks.ensure_live_console,
        callbacks.render_input_line,
        callbacks.refresh_input_line,
        callbacks.scroll_up,
        callbacks.scroll_down,
        callbacks.refresh_console);
}

template <class THandleExtendedKey,
          class THandleRegularShortcut,
          class TKeycodeToAscii,
          class TEnsureLiveConsole,
          class TToLowerAscii,
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
          class TStartSession,
          class TIsPrintable,
          class TOnEnter,
          class TOnPrintable,
          class TRefreshConsole>
struct KeyboardCallbacksRefT {
    THandleExtendedKey& handle_extended_key;
    THandleRegularShortcut& handle_regular_shortcut;
    TKeycodeToAscii& keycode_to_ascii;
    TEnsureLiveConsole& ensure_live_console;
    TToLowerAscii& to_lower_ascii;
    TAdvanceCandidate& advance_candidate;
    TReplaceCandidateText& replace_candidate_text;
    TCommitLearning& commit_learning;
    TClearCandidate& clear_candidate;
    TFlushRomaji& flush_romaji;
    TRenderInputLine& render_input_line;
    TRefreshInputLine& refresh_input_line;
    TResolveEntry& resolve_entry;
    TBuildPrefixEntry& build_prefix_entry;
    TIsEntryUsable& is_entry_usable;
    THasSelection& has_selection;
    TDeleteSelection& delete_selection;
    TStartSession& start_session;
    TIsPrintable& is_printable;
    TOnEnter& on_enter;
    TOnPrintable& on_printable;
    TRefreshConsole& refresh_console;

    KeyboardCallbacksRefT(THandleExtendedKey& handle_extended_key,
                          THandleRegularShortcut& handle_regular_shortcut,
                          TKeycodeToAscii& keycode_to_ascii,
                          TEnsureLiveConsole& ensure_live_console,
                          TToLowerAscii& to_lower_ascii,
                          TAdvanceCandidate& advance_candidate,
                          TReplaceCandidateText& replace_candidate_text,
                          TCommitLearning& commit_learning,
                          TClearCandidate& clear_candidate,
                          TFlushRomaji& flush_romaji,
                          TRenderInputLine& render_input_line,
                          TRefreshInputLine& refresh_input_line,
                          TResolveEntry& resolve_entry,
                          TBuildPrefixEntry& build_prefix_entry,
                          TIsEntryUsable& is_entry_usable,
                          THasSelection& has_selection,
                          TDeleteSelection& delete_selection,
                          TStartSession& start_session,
                          TIsPrintable& is_printable,
                          TOnEnter& on_enter,
                          TOnPrintable& on_printable,
                          TRefreshConsole& refresh_console)
        : handle_extended_key{handle_extended_key},
          handle_regular_shortcut{handle_regular_shortcut},
          keycode_to_ascii{keycode_to_ascii},
          ensure_live_console{ensure_live_console},
          to_lower_ascii{to_lower_ascii},
          advance_candidate{advance_candidate},
          replace_candidate_text{replace_candidate_text},
          commit_learning{commit_learning},
          clear_candidate{clear_candidate},
          flush_romaji{flush_romaji},
          render_input_line{render_input_line},
          refresh_input_line{refresh_input_line},
          resolve_entry{resolve_entry},
          build_prefix_entry{build_prefix_entry},
          is_entry_usable{is_entry_usable},
          has_selection{has_selection},
          delete_selection{delete_selection},
          start_session{start_session},
          is_printable{is_printable},
          on_enter{on_enter},
          on_printable{on_printable},
          refresh_console{refresh_console} {}
};

template <class TCandidateEntry,
          class THandleExtendedKey,
          class THandleRegularShortcut,
          class TKeycodeToAscii,
          class TEnsureLiveConsole,
          class TToLowerAscii,
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
          class TStartSession,
          class TIsPrintable,
          class TOnEnter,
          class TOnPrintable,
          class TRefreshConsole>
inline void HandleKeyboardRuntimeWithCallbacks(
    uint8_t keycode,
    const RuntimeKeyboardMessageContextT<TCandidateEntry>& context,
    const KeyboardCallbacksRefT<THandleExtendedKey,
                                THandleRegularShortcut,
                                TKeycodeToAscii,
                                TEnsureLiveConsole,
                                TToLowerAscii,
                                TAdvanceCandidate,
                                TReplaceCandidateText,
                                TCommitLearning,
                                TClearCandidate,
                                TFlushRomaji,
                                TRenderInputLine,
                                TRefreshInputLine,
                                TResolveEntry,
                                TBuildPrefixEntry,
                                TIsEntryUsable,
                                THasSelection,
                                TDeleteSelection,
                                TStartSession,
                                TIsPrintable,
                                TOnEnter,
                                TOnPrintable,
                                TRefreshConsole>& callbacks) {
    HandleKeyboardMessageRuntime(
        keycode,
        context,
        callbacks.handle_extended_key,
        callbacks.handle_regular_shortcut,
        callbacks.keycode_to_ascii,
        callbacks.ensure_live_console,
        callbacks.to_lower_ascii,
        callbacks.advance_candidate,
        callbacks.replace_candidate_text,
        callbacks.commit_learning,
        callbacks.clear_candidate,
        callbacks.flush_romaji,
        callbacks.render_input_line,
        callbacks.refresh_input_line,
        callbacks.resolve_entry,
        callbacks.build_prefix_entry,
        callbacks.is_entry_usable,
        callbacks.has_selection,
        callbacks.delete_selection,
        callbacks.start_session,
        callbacks.is_printable,
        callbacks.on_enter,
        callbacks.on_printable,
        callbacks.refresh_console);
}

}  // namespace input::dispatch

