#include "input/hid_keyboard.hpp"

namespace {

struct Set1Code {
    uint8_t code;
    bool extended;
};

bool ContainsUsage(const uint8_t* keys, uint8_t usage) {
    for (int i = 0; i < 6; ++i) {
        if (keys[i] == usage) {
            return true;
        }
    }
    return false;
}

bool AppendSet1(Set1Code sc, bool released, uint8_t* out, uint8_t* out_count, uint8_t max_out) {
    uint8_t need = sc.extended ? 2 : 1;
    if (static_cast<uint8_t>(*out_count + need) > max_out) {
        return false;
    }
    if (sc.extended) {
        out[*out_count] = 0xE0;
        ++(*out_count);
    }
    out[*out_count] = static_cast<uint8_t>(released ? (sc.code | 0x80u) : sc.code);
    ++(*out_count);
    return true;
}

bool MapModifierBitToSet1(int bit, Set1Code* out) {
    switch (bit) {
        case 0: out->code = 0x1D; out->extended = false; return true; // Left Ctrl
        case 1: out->code = 0x2A; out->extended = false; return true; // Left Shift
        case 2: out->code = 0x38; out->extended = false; return true; // Left Alt
        case 3: out->code = 0x5B; out->extended = true;  return true; // Left GUI
        case 4: out->code = 0x1D; out->extended = true;  return true; // Right Ctrl
        case 5: out->code = 0x36; out->extended = false; return true; // Right Shift
        case 6: out->code = 0x38; out->extended = true;  return true; // Right Alt
        case 7: out->code = 0x5C; out->extended = true;  return true; // Right GUI
        default: return false;
    }
}

bool MapHIDUsageToSet1(uint8_t usage, Set1Code* out) {
    switch (usage) {
        case 0x04: out->code = 0x1E; out->extended = false; return true; // A
        case 0x05: out->code = 0x30; out->extended = false; return true; // B
        case 0x06: out->code = 0x2E; out->extended = false; return true; // C
        case 0x07: out->code = 0x20; out->extended = false; return true; // D
        case 0x08: out->code = 0x12; out->extended = false; return true; // E
        case 0x09: out->code = 0x21; out->extended = false; return true; // F
        case 0x0A: out->code = 0x22; out->extended = false; return true; // G
        case 0x0B: out->code = 0x23; out->extended = false; return true; // H
        case 0x0C: out->code = 0x17; out->extended = false; return true; // I
        case 0x0D: out->code = 0x24; out->extended = false; return true; // J
        case 0x0E: out->code = 0x25; out->extended = false; return true; // K
        case 0x0F: out->code = 0x26; out->extended = false; return true; // L
        case 0x10: out->code = 0x32; out->extended = false; return true; // M
        case 0x11: out->code = 0x31; out->extended = false; return true; // N
        case 0x12: out->code = 0x18; out->extended = false; return true; // O
        case 0x13: out->code = 0x19; out->extended = false; return true; // P
        case 0x14: out->code = 0x10; out->extended = false; return true; // Q
        case 0x15: out->code = 0x13; out->extended = false; return true; // R
        case 0x16: out->code = 0x1F; out->extended = false; return true; // S
        case 0x17: out->code = 0x14; out->extended = false; return true; // T
        case 0x18: out->code = 0x16; out->extended = false; return true; // U
        case 0x19: out->code = 0x2F; out->extended = false; return true; // V
        case 0x1A: out->code = 0x11; out->extended = false; return true; // W
        case 0x1B: out->code = 0x2D; out->extended = false; return true; // X
        case 0x1C: out->code = 0x15; out->extended = false; return true; // Y
        case 0x1D: out->code = 0x2C; out->extended = false; return true; // Z
        case 0x1E: out->code = 0x02; out->extended = false; return true; // 1
        case 0x1F: out->code = 0x03; out->extended = false; return true; // 2
        case 0x20: out->code = 0x04; out->extended = false; return true; // 3
        case 0x21: out->code = 0x05; out->extended = false; return true; // 4
        case 0x22: out->code = 0x06; out->extended = false; return true; // 5
        case 0x23: out->code = 0x07; out->extended = false; return true; // 6
        case 0x24: out->code = 0x08; out->extended = false; return true; // 7
        case 0x25: out->code = 0x09; out->extended = false; return true; // 8
        case 0x26: out->code = 0x0A; out->extended = false; return true; // 9
        case 0x27: out->code = 0x0B; out->extended = false; return true; // 0
        case 0x28: out->code = 0x1C; out->extended = false; return true; // Enter
        case 0x29: out->code = 0x01; out->extended = false; return true; // Esc
        case 0x2A: out->code = 0x0E; out->extended = false; return true; // Backspace
        case 0x2B: out->code = 0x0F; out->extended = false; return true; // Tab
        case 0x2C: out->code = 0x39; out->extended = false; return true; // Space
        case 0x2D: out->code = 0x0C; out->extended = false; return true; // -
        case 0x2E: out->code = 0x0D; out->extended = false; return true; // =
        case 0x2F: out->code = 0x1A; out->extended = false; return true; // [
        case 0x30: out->code = 0x1B; out->extended = false; return true; // ]
        case 0x31: out->code = 0x2B; out->extended = false; return true; // Backslash
        case 0x33: out->code = 0x27; out->extended = false; return true; // ;
        case 0x34: out->code = 0x28; out->extended = false; return true; // '
        case 0x35: out->code = 0x29; out->extended = false; return true; // `
        case 0x36: out->code = 0x33; out->extended = false; return true; // ,
        case 0x37: out->code = 0x34; out->extended = false; return true; // .
        case 0x38: out->code = 0x35; out->extended = false; return true; // /
        case 0x39: out->code = 0x3A; out->extended = false; return true; // CapsLock
        case 0x3A: out->code = 0x3B; out->extended = false; return true; // F1
        case 0x3B: out->code = 0x3C; out->extended = false; return true; // F2
        case 0x3C: out->code = 0x3D; out->extended = false; return true; // F3
        case 0x3D: out->code = 0x3E; out->extended = false; return true; // F4
        case 0x3E: out->code = 0x3F; out->extended = false; return true; // F5
        case 0x3F: out->code = 0x40; out->extended = false; return true; // F6
        case 0x40: out->code = 0x41; out->extended = false; return true; // F7
        case 0x41: out->code = 0x42; out->extended = false; return true; // F8
        case 0x42: out->code = 0x43; out->extended = false; return true; // F9
        case 0x43: out->code = 0x44; out->extended = false; return true; // F10
        case 0x44: out->code = 0x57; out->extended = false; return true; // F11
        case 0x45: out->code = 0x58; out->extended = false; return true; // F12
        case 0x46: out->code = 0x37; out->extended = true;  return true; // PrintScreen (simplified)
        case 0x47: out->code = 0x46; out->extended = false; return true; // ScrollLock
        case 0x49: out->code = 0x52; out->extended = true;  return true; // Insert
        case 0x4A: out->code = 0x47; out->extended = true;  return true; // Home
        case 0x4B: out->code = 0x49; out->extended = true;  return true; // PageUp
        case 0x4C: out->code = 0x53; out->extended = true;  return true; // Delete
        case 0x4D: out->code = 0x4F; out->extended = true;  return true; // End
        case 0x4E: out->code = 0x51; out->extended = true;  return true; // PageDown
        case 0x4F: out->code = 0x4D; out->extended = true;  return true; // Right
        case 0x50: out->code = 0x4B; out->extended = true;  return true; // Left
        case 0x51: out->code = 0x50; out->extended = true;  return true; // Down
        case 0x52: out->code = 0x48; out->extended = true;  return true; // Up
        case 0x53: out->code = 0x45; out->extended = false; return true; // NumLock
        case 0x54: out->code = 0x35; out->extended = true;  return true; // Keypad /
        case 0x55: out->code = 0x37; out->extended = false; return true; // Keypad *
        case 0x56: out->code = 0x4A; out->extended = false; return true; // Keypad -
        case 0x57: out->code = 0x4E; out->extended = false; return true; // Keypad +
        case 0x58: out->code = 0x1C; out->extended = true;  return true; // Keypad Enter
        case 0x59: out->code = 0x4F; out->extended = false; return true; // Keypad 1
        case 0x5A: out->code = 0x50; out->extended = false; return true; // Keypad 2
        case 0x5B: out->code = 0x51; out->extended = false; return true; // Keypad 3
        case 0x5C: out->code = 0x4B; out->extended = false; return true; // Keypad 4
        case 0x5D: out->code = 0x4C; out->extended = false; return true; // Keypad 5
        case 0x5E: out->code = 0x4D; out->extended = false; return true; // Keypad 6
        case 0x5F: out->code = 0x47; out->extended = false; return true; // Keypad 7
        case 0x60: out->code = 0x48; out->extended = false; return true; // Keypad 8
        case 0x61: out->code = 0x49; out->extended = false; return true; // Keypad 9
        case 0x62: out->code = 0x52; out->extended = false; return true; // Keypad 0
        case 0x63: out->code = 0x53; out->extended = false; return true; // Keypad .
        case 0x64: out->code = 0x56; out->extended = false; return true; // Non-US key (ISO/JIS extra key)
        case 0x87: out->code = 0x73; out->extended = false; return true; // International1 (JIS Ro)
        case 0x89: out->code = 0x7D; out->extended = false; return true; // International3 (JIS Yen)
        case 0x8A: out->code = 0x79; out->extended = false; return true; // International4 (Henkan)
        case 0x8B: out->code = 0x7B; out->extended = false; return true; // International5 (Muhenkan)
        case 0x90: out->code = 0x70; out->extended = false; return true; // Lang1 (Kana)
        default:
            return false;
    }
}

bool IsLikelyKeyboardKeyUsage(uint8_t usage) {
    return usage == 0 || (usage >= 0x04 && usage <= 0x90);
}

bool LooksLikeKeyboardKeyArray(const uint8_t* keys) {
    int zero_count = 0;
    for (int i = 0; i < 6; ++i) {
        if (!IsLikelyKeyboardKeyUsage(keys[i])) {
            return false;
        }
        if (keys[i] == 0) {
            ++zero_count;
        }
    }
    // Typical boot-keyboard reports contain only a few simultaneous keys.
    return zero_count >= 3;
}

bool ExtractBootKeyboardPayload(const uint8_t* data, uint32_t len, uint32_t* out_off) {
    if (len >= 8) {
        if (data[1] == 0) {
            if (LooksLikeKeyboardKeyArray(&data[2])) {
                *out_off = 0;
                return true;
            }
        }
    }
    if (len >= 9) {
        if (data[2] == 0) {
            if (LooksLikeKeyboardKeyArray(&data[3])) {
                *out_off = 1;
                return true;
            }
        }
    }
    return false;
}

uint8_t g_prev_modifiers = 0;
uint8_t g_prev_keys[6] = {0, 0, 0, 0, 0, 0};
uint8_t g_repeat_usage = 0;
uint8_t g_repeat_hold_count = 0;
uint8_t g_repeat_interval_count = 0;
const uint8_t kInitialRepeatDelayReports = 12;
const uint8_t kRepeatIntervalReports = 3;

uint8_t FindRepeatCandidate(const uint8_t* keys) {
    for (int i = 0; i < 6; ++i) {
        const uint8_t usage = keys[i];
        if (usage == 0) {
            continue;
        }
        Set1Code sc{};
        if (MapHIDUsageToSet1(usage, &sc)) {
            return usage;
        }
    }
    return 0;
}

}  // namespace

bool DecodeHIDBootKeyboardToSet1(const uint8_t* data,
                                 uint32_t len,
                                 uint8_t* out_scancodes,
                                 uint8_t* out_count,
                                 uint8_t max_out) {
    if (data == nullptr || out_scancodes == nullptr || out_count == nullptr || max_out == 0) {
        return false;
    }
    *out_count = 0;

    uint32_t off = 0;
    if (!ExtractBootKeyboardPayload(data, len, &off)) {
        return false;
    }

    const uint8_t modifiers = data[off + 0];
    uint8_t keys[6];
    for (int i = 0; i < 6; ++i) {
        keys[i] = data[off + 2 + i];
    }

    // Modifiers: emit make/break on bit transitions.
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t mask = static_cast<uint8_t>(1u << bit);
        const bool prev = (g_prev_modifiers & mask) != 0;
        const bool cur = (modifiers & mask) != 0;
        if (prev == cur) {
            continue;
        }
        Set1Code sc{};
        if (!MapModifierBitToSet1(bit, &sc)) {
            continue;
        }
        AppendSet1(sc, !cur, out_scancodes, out_count, max_out);
    }

    // Released non-modifier keys.
    for (int i = 0; i < 6; ++i) {
        const uint8_t usage = g_prev_keys[i];
        if (usage == 0 || ContainsUsage(keys, usage)) {
            continue;
        }
        Set1Code sc{};
        if (!MapHIDUsageToSet1(usage, &sc)) {
            continue;
        }
        AppendSet1(sc, true, out_scancodes, out_count, max_out);
    }

    // Pressed non-modifier keys.
    for (int i = 0; i < 6; ++i) {
        const uint8_t usage = keys[i];
        if (usage == 0 || ContainsUsage(g_prev_keys, usage)) {
            continue;
        }
        Set1Code sc{};
        if (!MapHIDUsageToSet1(usage, &sc)) {
            continue;
        }
        AppendSet1(sc, false, out_scancodes, out_count, max_out);
    }

    // Software repeat for HID boot keyboards: unchanged reports imply "key still held".
    const bool modifiers_changed = (modifiers != g_prev_modifiers);
    bool keys_changed = false;
    for (int i = 0; i < 6; ++i) {
        if (keys[i] != g_prev_keys[i]) {
            keys_changed = true;
            break;
        }
    }
    if (modifiers_changed || keys_changed) {
        g_repeat_usage = FindRepeatCandidate(keys);
        g_repeat_hold_count = 0;
        g_repeat_interval_count = 0;
    } else if (g_repeat_usage != 0 && ContainsUsage(keys, g_repeat_usage)) {
        if (g_repeat_hold_count < kInitialRepeatDelayReports) {
            ++g_repeat_hold_count;
        } else {
            ++g_repeat_interval_count;
            if (g_repeat_interval_count >= kRepeatIntervalReports) {
                g_repeat_interval_count = 0;
                Set1Code sc{};
                if (MapHIDUsageToSet1(g_repeat_usage, &sc)) {
                    AppendSet1(sc, false, out_scancodes, out_count, max_out);
                }
            }
        }
    } else {
        g_repeat_usage = FindRepeatCandidate(keys);
        g_repeat_hold_count = 0;
        g_repeat_interval_count = 0;
    }

    g_prev_modifiers = modifiers;
    for (int i = 0; i < 6; ++i) {
        g_prev_keys[i] = keys[i];
    }
    return true;
}
