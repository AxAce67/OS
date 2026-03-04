#include "input/key_handler.hpp"

namespace input {

RegularShortcutAction DecideRegularShortcutAction(uint8_t key,
                                                  bool ctrl_pressed,
                                                  bool num_lock) {
    if (ctrl_pressed) {
        if (key == 0x39) return RegularShortcutAction::kCtrlSpace;
        if (key == 0x1E) return RegularShortcutAction::kCtrlA;
        if (key == 0x12) return RegularShortcutAction::kCtrlE;
        if (key == 0x26) return RegularShortcutAction::kCtrlL;
    }
    if (key == 0x01) return RegularShortcutAction::kEsc;
    if (!num_lock && key == 0x47) return RegularShortcutAction::kHomeFallback;
    if (!num_lock && key == 0x48) return RegularShortcutAction::kUpFallback;
    if (!num_lock && key == 0x4F) return RegularShortcutAction::kEndFallback;
    if (!num_lock && key == 0x50) return RegularShortcutAction::kDownFallback;
    if (key == 0x0E) return RegularShortcutAction::kBackspace;
    if (!num_lock && (key == 0x53 || key == 0x71)) return RegularShortcutAction::kDelete;
    if (key == 0x0F) return RegularShortcutAction::kTab;
    if (key == 0x29) return RegularShortcutAction::kHankakuZenkaku;
    if (key == 0x70) return RegularShortcutAction::kKana;
    if (key == 0x79) return RegularShortcutAction::kHenkan;
    if (key == 0x7B) return RegularShortcutAction::kMuhenkan;
    return RegularShortcutAction::kNone;
}

ExtendedKeyAction DecideExtendedKeyAction(uint8_t key) {
    if (key == 0x49) return ExtendedKeyAction::kPageUp;
    if (key == 0x51) return ExtendedKeyAction::kPageDown;
    if (key == 0x53) return ExtendedKeyAction::kDelete;
    if (key == 0x4B) return ExtendedKeyAction::kLeft;
    if (key == 0x4D) return ExtendedKeyAction::kRight;
    if (key == 0x47) return ExtendedKeyAction::kHome;
    if (key == 0x4F) return ExtendedKeyAction::kEnd;
    if (key == 0x48) return ExtendedKeyAction::kUp;
    if (key == 0x50) return ExtendedKeyAction::kDown;
    return ExtendedKeyAction::kNone;
}

ImeModeState ApplyImeModeAction(RegularShortcutAction action,
                                bool ime_enabled,
                                bool jp_layout) {
    ImeModeState out{ime_enabled, jp_layout, false};
    switch (action) {
    case RegularShortcutAction::kCtrlSpace:
    case RegularShortcutAction::kKana:
        out.ime_enabled = !ime_enabled;
        if (out.ime_enabled) {
            out.jp_layout = true;
        }
        out.changed = (out.ime_enabled != ime_enabled) || (out.jp_layout != jp_layout);
        return out;
    case RegularShortcutAction::kHankakuZenkaku:
        out.ime_enabled = !ime_enabled;
        out.jp_layout = true;
        out.changed = (out.ime_enabled != ime_enabled) || (out.jp_layout != jp_layout);
        return out;
    case RegularShortcutAction::kHenkan:
        out.ime_enabled = true;
        out.jp_layout = true;
        out.changed = (out.ime_enabled != ime_enabled) || (out.jp_layout != jp_layout);
        return out;
    case RegularShortcutAction::kMuhenkan:
        out.ime_enabled = false;
        out.changed = (out.ime_enabled != ime_enabled);
        return out;
    default:
        return out;
    }
}

RegularExecPlan BuildRegularExecPlan(RegularShortcutAction action,
                                     bool ime_enabled,
                                     int ime_romaji_len) {
    RegularExecPlan p{};
    p.handled = false;
    p.flush_romaji = false;
    p.ensure_live_console = false;
    p.clear_selection = false;
    p.kind = RegularExecKind::kNone;

    switch (action) {
    case RegularShortcutAction::kCtrlA:
    case RegularShortcutAction::kHomeFallback:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.clear_selection = true;
        p.kind = RegularExecKind::kMoveCursorStart;
        return p;
    case RegularShortcutAction::kCtrlE:
    case RegularShortcutAction::kEndFallback:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.clear_selection = true;
        p.kind = RegularExecKind::kMoveCursorEnd;
        return p;
    case RegularShortcutAction::kUpFallback:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.kind = RegularExecKind::kHistoryUpWithCandidate;
        return p;
    case RegularShortcutAction::kDownFallback:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.kind = RegularExecKind::kHistoryDownWithCandidate;
        return p;
    case RegularShortcutAction::kBackspace:
        p.handled = true;
        p.ensure_live_console = true;
        p.kind = RegularExecKind::kBackspace;
        return p;
    case RegularShortcutAction::kDelete:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.kind = RegularExecKind::kDelete;
        return p;
    case RegularShortcutAction::kTab:
        p.handled = true;
        p.flush_romaji = ime_enabled && ime_romaji_len > 0;
        p.ensure_live_console = true;
        p.kind = RegularExecKind::kTab;
        return p;
    default:
        return p;
    }
}

}  // namespace input
