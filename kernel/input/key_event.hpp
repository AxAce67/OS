#pragma once

#include <stdint.h>

struct KeyboardModifiers {
    bool left_shift;
    bool right_shift;
    bool caps_lock;
    bool num_lock;
    bool left_ctrl;
    bool right_ctrl;
};

enum class KeyEventKind : uint8_t {
    kNone = 0,
    kModifier,
    kKey,
};

struct KeyEvent {
    KeyEventKind kind;
    uint8_t keycode;     // Set-1 base code (without release bit)
    bool extended;       // true when prefixed by E0
    bool released;       // true on key release
    bool shift;
    bool ctrl;
    bool caps_lock;
    bool num_lock;
};

void InitKeyboardModifiers(KeyboardModifiers* mods);
bool DecodePS2Set1KeyEvent(uint8_t raw_scancode,
                           bool* e0_prefix,
                           KeyboardModifiers* mods,
                           KeyEvent* out);
bool IsShiftPressed(const KeyboardModifiers& mods);
bool IsCtrlPressed(const KeyboardModifiers& mods);
