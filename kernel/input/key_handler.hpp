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

RegularShortcutAction DecideRegularShortcutAction(uint8_t key,
                                                  bool ctrl_pressed,
                                                  bool num_lock);

ExtendedKeyAction DecideExtendedKeyAction(uint8_t key);

ImeModeState ApplyImeModeAction(RegularShortcutAction action,
                                bool ime_enabled,
                                bool jp_layout);

}  // namespace input
