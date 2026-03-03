#include "input/key_event.hpp"

namespace {

bool ApplyModifierSet1(uint8_t keycode, bool extended, bool released, KeyboardModifiers* mods) {
    if (!extended) {
        switch (keycode) {
            case 0x2A:
                mods->left_shift = !released;
                return true;
            case 0x36:
                mods->right_shift = !released;
                return true;
            case 0x1D:
                mods->left_ctrl = !released;
                return true;
            case 0x3A:
                if (!released) {
                    mods->caps_lock = !mods->caps_lock;
                }
                return true;
            default:
                break;
        }
    } else if (keycode == 0x1D) {
        mods->right_ctrl = !released;
        return true;
    }
    return false;
}

}  // namespace

void InitKeyboardModifiers(KeyboardModifiers* mods) {
    mods->left_shift = false;
    mods->right_shift = false;
    mods->caps_lock = false;
    mods->left_ctrl = false;
    mods->right_ctrl = false;
}

bool IsShiftPressed(const KeyboardModifiers& mods) {
    return mods.left_shift || mods.right_shift;
}

bool IsCtrlPressed(const KeyboardModifiers& mods) {
    return mods.left_ctrl || mods.right_ctrl;
}

bool DecodePS2Set1KeyEvent(uint8_t raw_scancode,
                           bool* e0_prefix,
                           KeyboardModifiers* mods,
                           KeyEvent* out) {
    out->kind = KeyEventKind::kNone;
    out->keycode = 0;
    out->extended = false;
    out->released = false;
    out->shift = IsShiftPressed(*mods);
    out->ctrl = IsCtrlPressed(*mods);
    out->caps_lock = mods->caps_lock;

    if (raw_scancode == 0xE0) {
        *e0_prefix = true;
        return false;
    }

    const bool extended = *e0_prefix;
    *e0_prefix = false;
    const bool released = (raw_scancode & 0x80) != 0;
    const uint8_t keycode = raw_scancode & 0x7F;

    out->keycode = keycode;
    out->extended = extended;
    out->released = released;

    if (ApplyModifierSet1(keycode, extended, released, mods)) {
        out->kind = KeyEventKind::kModifier;
        out->shift = IsShiftPressed(*mods);
        out->ctrl = IsCtrlPressed(*mods);
        out->caps_lock = mods->caps_lock;
        return true;
    }

    out->kind = KeyEventKind::kKey;
    out->shift = IsShiftPressed(*mods);
    out->ctrl = IsCtrlPressed(*mods);
    out->caps_lock = mods->caps_lock;
    return true;
}

