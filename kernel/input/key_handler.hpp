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

RegularShortcutAction DecideRegularShortcutAction(uint8_t key,
                                                  bool ctrl_pressed,
                                                  bool num_lock);

ExtendedKeyAction DecideExtendedKeyAction(uint8_t key);

}  // namespace input

