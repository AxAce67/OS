#pragma once

#include <stdint.h>

namespace input {

enum class RegularShortcutAction {
    kNone = 0,
    kEsc,
    kCtrlSpace,
    kCtrlA,
    kCtrlE,
    kCtrlL,
    kHomeFallback,
    kUpFallback,
    kEndFallback,
    kDownFallback,
    kBackspace,
    kDelete,
    kTab,
    kHankakuZenkaku,
    kKana,
    kHenkan,
    kMuhenkan,
};

enum class ExtendedKeyAction {
    kNone = 0,
    kPageUp,
    kPageDown,
    kDelete,
    kLeft,
    kRight,
    kHome,
    kEnd,
    kUp,
    kDown,
};

struct ImeModeState {
    bool ime_enabled;
    bool jp_layout;
    bool changed;
};

enum class RegularExecKind {
    kNone = 0,
    kMoveCursorStart,
    kMoveCursorEnd,
    kHistoryUpWithCandidate,
    kHistoryDownWithCandidate,
    kBackspace,
    kDelete,
    kTab,
};

struct RegularExecPlan {
    bool handled;
    bool flush_romaji;
    bool ensure_live_console;
    bool clear_selection;
    RegularExecKind kind;
};

enum class ExtendedExecKind {
    kNone = 0,
    kPageUp,
    kPageDown,
    kDelete,
    kMoveCursorLeft,
    kMoveCursorRight,
    kMoveCursorStart,
    kMoveCursorEnd,
    kHistoryUp,
    kHistoryDown,
};

struct ExtendedExecPlan {
    bool handled;
    bool flush_romaji;
    bool clear_candidate;
    bool ensure_live_console;
    bool clear_selection;
    ExtendedExecKind kind;
};

RegularShortcutAction DecideRegularShortcutAction(uint8_t key,
                                                  bool ctrl_pressed,
                                                  bool num_lock);

ExtendedKeyAction DecideExtendedKeyAction(uint8_t key);

ImeModeState ApplyImeModeAction(RegularShortcutAction action,
                                bool ime_enabled,
                                bool jp_layout);

RegularExecPlan BuildRegularExecPlan(RegularShortcutAction action,
                                     bool ime_enabled,
                                     int ime_romaji_len);

ExtendedExecPlan BuildExtendedExecPlan(ExtendedKeyAction action,
                                       bool ime_enabled,
                                       int ime_romaji_len,
                                       bool candidate_active,
                                       bool has_candidate_nav);

}  // namespace input
