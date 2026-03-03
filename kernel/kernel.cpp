// kernel.c
#include <stdint.h>
#include "frame_buffer_config.h"
#include "font.h"

// ピクセルを一つ塗る関数（カーネル版）
void DrawPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= config->horizontal_resolution || y >= config->vertical_resolution) return;

    // config->pixels_per_scan_line はパディングを含んだ1行の論理的なピクセル数
    uint32_t index = (y * config->pixels_per_scan_line + x) * 4;
    
    if (config->pixel_format == kPixelRGBResv8BitPerColor) {
        config->frame_buffer[index]     = r; // Red
        config->frame_buffer[index + 1] = g; // Green
        config->frame_buffer[index + 2] = b; // Blue
    } else {
        config->frame_buffer[index]     = b; // Blue
        config->frame_buffer[index + 1] = g; // Green
        config->frame_buffer[index + 2] = r; // Red
    }
}

// ピクセルを一つ読み取る関数（背景保存用）
void ReadPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (x >= config->horizontal_resolution || y >= config->vertical_resolution) {
        r = g = b = 0;
        return;
    }

    uint32_t index = (y * config->pixels_per_scan_line + x) * 4;
    if (config->pixel_format == kPixelRGBResv8BitPerColor) {
        r = config->frame_buffer[index];
        g = config->frame_buffer[index + 1];
        b = config->frame_buffer[index + 2];
    } else {
        b = config->frame_buffer[index];
        g = config->frame_buffer[index + 1];
        r = config->frame_buffer[index + 2];
    }
}

// 文字を一つ描画する関数
void DrawChar(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, char c, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t* font_data = kFont[(uint8_t)c];
    for (int dy = 0; dy < 16; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
            if ((font_data[dy] << dx) & 0x80) { // 上位ビットから順にピクセルがONか確認
                DrawPixel(config, start_x + dx, start_y + dy, r, g, b);
            }
        }
    }
}

// 文字列を描画する関数
void DrawString(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, const char* str, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t x = start_x;
    for (int i = 0; str[i] != '\0'; ++i) {
        DrawChar(config, x, start_y, str[i], r, g, b);
        x += 8; // 1文字進める
    }
}

#include "console.hpp"
#include "mouse.hpp"
#include "interrupt.hpp"
#include "interrupt_handler.hpp"
#include "pic.hpp"
#include "ps2.hpp"
#include "pci.hpp"
#include "io.hpp"
#include "usb/xhci.hpp"
#include "queue.hpp"
#include "input/message.hpp"
#include "boot_info.h"
#include "memory.hpp"
#include "paging.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "apic.hpp"
#include "timer.hpp"
#include "shell/commands.hpp"
#include "shell/cmd_dispatch.hpp"
#include "shell/text.hpp"

extern Console* console;

namespace {
struct AllocationHeader {
    uint64_t magic;
    uint64_t num_pages;
};

const uint64_t kAllocationMagic = 0x4F53414C4C4F4341ULL;  // "OSALLOCA"
}

extern ArrayQueue<Message, 256>* main_queue;
extern MouseCursor* mouse_cursor;
extern LayerManager* layer_manager;

namespace {
struct KeyboardState {
    bool left_shift;
    bool right_shift;
    bool caps_lock;
    bool left_ctrl;
    bool right_ctrl;
};

struct ShellPair {
    bool used;
    char key[32];
    char value[96];
};

struct ShellDir {
    bool used;
    char path[96];
};

struct ShellFile {
    bool used;
    char path[96];
    uint64_t size;
    uint8_t data[2048];
};

ShellPair g_vars[16];
ShellPair g_aliases[16];
ShellDir g_dirs[32];
ShellFile g_files[64];
bool g_key_repeat_enabled = true;
bool g_jp_layout = false;
const BootInfo* g_boot_info = nullptr;
bool g_dirs_initialized = false;
char g_cwd[96] = "/";
XHCICapabilityInfo g_xhci_caps = {};
uint8_t g_last_xhci_slot_id = 0;
bool g_xhci_hid_auto_enabled = false;
uint8_t g_xhci_hid_auto_slot = 0;
uint32_t g_xhci_hid_auto_len = 8;
uint64_t g_xhci_hid_last_poll_tick = 0;
uint16_t g_xhci_hid_auto_mps = 8;
uint8_t g_xhci_hid_auto_interval = 4;
uint32_t g_xhci_hid_auto_consecutive_failures = 0;
uint64_t g_xhci_hid_auto_fail_count = 0;
uint64_t g_xhci_hid_auto_recover_count = 0;
uint64_t g_xhci_hid_next_recover_tick = 0;
bool g_boot_mouse_auto_enabled = true;
const uint32_t kAutoStartHIDLen = 8;
const uint16_t kAutoStartHIDMps = 8;
const uint8_t kAutoStartHIDInterval = 4;
uint8_t g_hid_format_mode = 0;  // 0=unknown,1=A,2=B
uint32_t g_hid_observed_max_raw = 0;
uint32_t g_hid_sample_count = 0;
bool g_hid_calibrated = false;
uint16_t g_hid_min_x = 0xFFFF;
uint16_t g_hid_min_y = 0xFFFF;
uint16_t g_hid_max_x = 0;
uint16_t g_hid_max_y = 0;
int g_hid_smooth_x = -1;
int g_hid_smooth_y = -1;
const int kHidSmoothAlphaNum = 1;  // 1/4 EMA
const int kHidSmoothAlphaDen = 4;
uint8_t g_hid_buttons_mask = 0;
uint8_t g_mouse_buttons_current = 0;
uint64_t g_mouse_left_press_count = 0;
uint64_t g_mouse_right_press_count = 0;
uint64_t g_mouse_middle_press_count = 0;
uint64_t g_last_absolute_mouse_tick = 0;
int g_last_abs_dispatched_x = -1;
int g_last_abs_dispatched_y = -1;
uint8_t g_last_abs_dispatched_buttons = 0;

int ClampInt(int v, int min_v, int max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

uint16_t ClampU16(uint16_t v, uint16_t min_v, uint16_t max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

int ScreenWidth() {
    if (g_boot_info == nullptr || g_boot_info->frame_buffer_config == nullptr) {
        return 1;
    }
    return static_cast<int>(g_boot_info->frame_buffer_config->horizontal_resolution);
}

int ScreenHeight() {
    if (g_boot_info == nullptr || g_boot_info->frame_buffer_config == nullptr) {
        return 1;
    }
    return static_cast<int>(g_boot_info->frame_buffer_config->vertical_resolution);
}

bool DecodeHIDAbsoluteXY(const uint8_t* data, uint32_t len, int* out_x, int* out_y, int* out_wheel, uint8_t* out_buttons) {
    if (data == nullptr || out_x == nullptr || out_y == nullptr || len < 5) {
        return false;
    }
    *out_wheel = 0;
    if (out_buttons != nullptr) {
        *out_buttons = 0;
    }

    // Candidate A: [buttons][x_lo][x_hi][y_lo][y_hi](...)
    uint16_t ax = static_cast<uint16_t>(data[1] | (static_cast<uint16_t>(data[2]) << 8));
    uint16_t ay = static_cast<uint16_t>(data[3] | (static_cast<uint16_t>(data[4]) << 8));

    // Candidate B: [report_id][buttons][x_lo][x_hi][y_lo][y_hi](...)
    uint16_t bx = 0;
    uint16_t by = 0;
    bool has_b = false;
    if (len >= 6) {
        bx = static_cast<uint16_t>(data[2] | (static_cast<uint16_t>(data[3]) << 8));
        by = static_cast<uint16_t>(data[4] | (static_cast<uint16_t>(data[5]) << 8));
        has_b = true;
    }

    uint16_t raw_x = ax;
    uint16_t raw_y = ay;
    uint8_t chosen_mode = 1;
    if (has_b) {
        const bool a_reasonable = (ax <= 0x7FFFu && ay <= 0x7FFFu);
        const bool b_reasonable = (bx <= 0x7FFFu && by <= 0x7FFFu);
        if (g_hid_format_mode == 2) {
            raw_x = bx;
            raw_y = by;
            chosen_mode = 2;
        } else if (g_hid_format_mode == 1) {
            raw_x = ax;
            raw_y = ay;
            chosen_mode = 1;
        } else if (!a_reasonable && b_reasonable) {
            raw_x = bx;
            raw_y = by;
            chosen_mode = 2;
            g_hid_format_mode = 2;
        } else {
            raw_x = ax;
            raw_y = ay;
            chosen_mode = 1;
            if (!b_reasonable || a_reasonable) {
                g_hid_format_mode = 1;
            }
        }
    } else if (g_hid_format_mode == 0) {
        g_hid_format_mode = 1;
    }

    if (out_buttons != nullptr) {
        if (chosen_mode == 2 && len >= 2) {
            *out_buttons = data[1];
        } else {
            *out_buttons = data[0];
        }
    }

    if (raw_x > g_hid_observed_max_raw) g_hid_observed_max_raw = raw_x;
    if (raw_y > g_hid_observed_max_raw) g_hid_observed_max_raw = raw_y;
    ++g_hid_sample_count;

    uint32_t max_raw = 0xFFFFu;
    if (g_hid_observed_max_raw <= 0x7FFFu && g_hid_sample_count >= 8) {
        max_raw = 0x7FFFu;
    }
    if (g_hid_observed_max_raw > 0x7FFFu) {
        max_raw = 0xFFFFu;
    }
    if (chosen_mode == 2 && max_raw == 0xFFFFu && raw_x <= 0x7FFFu && raw_y <= 0x7FFFu) {
        // ReportID付き形式で16bit full rangeが使われないケースが多いので補正
        max_raw = 0x7FFFu;
    }

    if (raw_x < g_hid_min_x) g_hid_min_x = raw_x;
    if (raw_y < g_hid_min_y) g_hid_min_y = raw_y;
    if (raw_x > g_hid_max_x) g_hid_max_x = raw_x;
    if (raw_y > g_hid_max_y) g_hid_max_y = raw_y;
    if (g_hid_sample_count >= 12) {
        g_hid_calibrated = true;
    }

    uint16_t use_min_x = 0;
    uint16_t use_min_y = 0;
    uint16_t use_max_x = static_cast<uint16_t>(max_raw);
    uint16_t use_max_y = static_cast<uint16_t>(max_raw);
    if (g_hid_calibrated) {
        // 少しだけ余白を持たせて端への到達性を上げる
        const uint16_t pad_x = (g_hid_max_x > g_hid_min_x) ? static_cast<uint16_t>((g_hid_max_x - g_hid_min_x) / 20) : 0;
        const uint16_t pad_y = (g_hid_max_y > g_hid_min_y) ? static_cast<uint16_t>((g_hid_max_y - g_hid_min_y) / 20) : 0;
        use_min_x = (g_hid_min_x > pad_x) ? static_cast<uint16_t>(g_hid_min_x - pad_x) : 0;
        use_min_y = (g_hid_min_y > pad_y) ? static_cast<uint16_t>(g_hid_min_y - pad_y) : 0;
        use_max_x = ClampU16(static_cast<uint16_t>(g_hid_max_x + pad_x), use_min_x + 1, static_cast<uint16_t>(max_raw));
        use_max_y = ClampU16(static_cast<uint16_t>(g_hid_max_y + pad_y), use_min_y + 1, static_cast<uint16_t>(max_raw));
    }

    uint16_t cx = ClampU16(raw_x, use_min_x, use_max_x);
    uint16_t cy = ClampU16(raw_y, use_min_y, use_max_y);

    const int w = ScreenWidth();
    const int h = ScreenHeight();
    const uint32_t range_x = static_cast<uint32_t>(use_max_x - use_min_x);
    const uint32_t range_y = static_cast<uint32_t>(use_max_y - use_min_y);
    int px = 0;
    int py = 0;
    if (range_x > 0) {
        px = static_cast<int>((static_cast<uint64_t>(cx - use_min_x) * (w - 1)) / range_x);
    }
    if (range_y > 0) {
        py = static_cast<int>((static_cast<uint64_t>(cy - use_min_y) * (h - 1)) / range_y);
    }
    px = ClampInt(px, 0, w - 1);
    py = ClampInt(py, 0, h - 1);

    if (g_hid_smooth_x < 0 || g_hid_smooth_y < 0) {
        g_hid_smooth_x = px;
        g_hid_smooth_y = py;
    } else {
        g_hid_smooth_x = (g_hid_smooth_x * (kHidSmoothAlphaDen - kHidSmoothAlphaNum) + px * kHidSmoothAlphaNum) / kHidSmoothAlphaDen;
        g_hid_smooth_y = (g_hid_smooth_y * (kHidSmoothAlphaDen - kHidSmoothAlphaNum) + py * kHidSmoothAlphaNum) / kHidSmoothAlphaDen;
    }

    *out_x = g_hid_smooth_x;
    *out_y = g_hid_smooth_y;

    if (chosen_mode == 2 && len >= 7) {
        *out_wheel = static_cast<int>(static_cast<int8_t>(data[6]));
    } else if (chosen_mode == 1 && len >= 6) {
        *out_wheel = static_cast<int>(static_cast<int8_t>(data[5]));
    }
    return true;
}

void ResetHIDDecodeLearning() {
    g_hid_format_mode = 0;
    g_hid_observed_max_raw = 0;
    g_hid_sample_count = 0;
    g_hid_calibrated = false;
    g_hid_min_x = 0xFFFF;
    g_hid_min_y = 0xFFFF;
    g_hid_max_x = 0;
    g_hid_max_y = 0;
    g_hid_smooth_x = -1;
    g_hid_smooth_y = -1;
    g_hid_buttons_mask = 0;
    g_last_abs_dispatched_x = -1;
    g_last_abs_dispatched_y = -1;
    g_last_abs_dispatched_buttons = 0;
}

bool PollHIDAndApply(uint8_t slot, uint32_t req_len, bool verbose, uint32_t timeout_iters = 3000000) {
    XHCIInterruptInResult rr{};
    if (!XHCIPollInterruptIn(g_xhci_caps, slot, req_len, &rr, timeout_iters)) {
        if (verbose) {
            console->PrintLine("xhcihidpoll: timeout/fail");
        }
        return false;
    }
    if (!rr.ok) {
        if (verbose) {
            console->Print("xhcihidpoll: transfer ccode=");
            console->PrintDec(rr.completion_code);
            console->Print("\n");
        }
        return false;
    }

    int x = 0;
    int y = 0;
    int wheel = 0;
    uint8_t buttons = 0;
    if (!DecodeHIDAbsoluteXY(rr.data, rr.data_length, &x, &y, &wheel, &buttons)) {
        if (verbose) {
            console->Print("xhcihidpoll: raw=");
            for (uint32_t i = 0; i < rr.data_length; ++i) {
                console->PrintHex(rr.data[i], 2);
                if (i + 1 < rr.data_length) {
                    console->Print(" ");
                }
            }
            console->Print("\n");
        }
        return false;
    }
    g_hid_buttons_mask = buttons;
    g_last_absolute_mouse_tick = CurrentTick();

    // Suppress tiny absolute-pointer jitter to avoid cursor flicker while typing.
    if (g_last_abs_dispatched_x >= 0 && g_last_abs_dispatched_y >= 0) {
        const int dx = x - g_last_abs_dispatched_x;
        const int dy = y - g_last_abs_dispatched_y;
        const int adx = (dx < 0) ? -dx : dx;
        const int ady = (dy < 0) ? -dy : dy;
        if (wheel == 0 &&
            buttons == g_last_abs_dispatched_buttons &&
            adx <= 3 && ady <= 3) {
            return true;
        }
    }

    EnqueueAbsolutePointerEvent(x, y, wheel, buttons);
    g_last_abs_dispatched_x = x;
    g_last_abs_dispatched_y = y;
    g_last_abs_dispatched_buttons = buttons;
    if (verbose) {
        console->Print("xhcihidpoll: x=");
        console->PrintDec(x);
        console->Print(" y=");
        console->PrintDec(y);
        console->Print(" wheel=");
        console->PrintDec(wheel);
        console->Print(" btn=0x");
        console->PrintHex(buttons, 2);
        console->Print("\n");
    }
    return true;
}

bool FindFirstConnectedPort(int* out_port, int* out_speed) {
    if (out_port == nullptr || out_speed == nullptr || !g_xhci_caps.valid) {
        return false;
    }
    XHCIPortStatus ports[32];
    const int n = ReadXHCIPortStatus(g_xhci_caps, ports, 32);
    for (int i = 0; i < n; ++i) {
        if (ports[i].connected) {
            *out_port = static_cast<int>(ports[i].port_id);
            *out_speed = static_cast<int>(ports[i].speed);
            return true;
        }
    }
    return false;
}

bool StartXHCIAutoMouse(uint32_t req_len, uint16_t mps, uint8_t interval) {
    if (!g_xhci_caps.valid) {
        return false;
    }
    if (!XHCIInitializeCommandAndEventRings(g_xhci_caps)) {
        return false;
    }

    XHCICommandResult slot_result{};
    if (!XHCIEnableSlot(g_xhci_caps, &slot_result) || !slot_result.ok || slot_result.slot_id == 0) {
        return false;
    }
    g_last_xhci_slot_id = slot_result.slot_id;

    int port = 0;
    int speed = 0;
    if (!FindFirstConnectedPort(&port, &speed)) {
        return false;
    }

    XHCIAddressDeviceResult addr_result{};
    if (!XHCIAddressDevice(g_xhci_caps,
                           slot_result.slot_id,
                           static_cast<uint8_t>(port),
                           static_cast<uint8_t>(speed),
                           &addr_result) ||
        !addr_result.ok) {
        return false;
    }

    XHCIConfigureEndpointResult cfg_result{};
    if (!XHCIConfigureInterruptInEndpoint(g_xhci_caps,
                                          slot_result.slot_id,
                                          mps,
                                          interval,
                                          &cfg_result) ||
        !cfg_result.ok) {
        return false;
    }

    g_xhci_hid_auto_slot = slot_result.slot_id;
    g_xhci_hid_auto_len = req_len;
    g_xhci_hid_auto_mps = mps;
    g_xhci_hid_auto_interval = interval;
    g_xhci_hid_auto_enabled = true;
    g_xhci_hid_last_poll_tick = CurrentTick();
    g_xhci_hid_auto_consecutive_failures = 0;
    g_xhci_hid_next_recover_tick = 0;

    // 起動直後の追従精度を上げるため、短いタイムアウトで数サンプルを先に学習する。
    for (int i = 0; i < 8; ++i) {
        PollHIDAndApply(g_xhci_hid_auto_slot, g_xhci_hid_auto_len, false, 200000);
    }
    return true;
}

char KeycodeToAscii(uint8_t keycode, bool shift, bool caps_lock) {
    if (g_jp_layout) {
        switch (keycode) {
            case 0x02: return shift ? '!' : '1';
            case 0x03: return shift ? '"' : '2';
            case 0x04: return shift ? '#' : '3';
            case 0x05: return shift ? '$' : '4';
            case 0x06: return shift ? '%' : '5';
            case 0x07: return shift ? '&' : '6';
            case 0x08: return shift ? '\'' : '7';
            case 0x09: return shift ? '(' : '8';
            case 0x0A: return shift ? ')' : '9';
            case 0x0B: return shift ? ')' : '0';
            case 0x0C: return shift ? '=' : '-';
            case 0x0D: return shift ? '~' : '^';
            case 0x1A: return shift ? '`' : '@';
            case 0x1B: return shift ? '{' : '[';
            case 0x27: return shift ? '+' : ';';
            case 0x28: return shift ? '*' : ':';
            case 0x29: return shift ? '|' : '\\';
            case 0x2B: return shift ? '}' : ']';
            case 0x33: return shift ? '<' : ',';
            case 0x34: return shift ? '>' : '.';
            case 0x35: return shift ? '?' : '/';
            default: break;
        }
    }
    switch (keycode) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return (shift ^ caps_lock) ? 'Q' : 'q';
        case 0x11: return (shift ^ caps_lock) ? 'W' : 'w';
        case 0x12: return (shift ^ caps_lock) ? 'E' : 'e';
        case 0x13: return (shift ^ caps_lock) ? 'R' : 'r';
        case 0x14: return (shift ^ caps_lock) ? 'T' : 't';
        case 0x15: return (shift ^ caps_lock) ? 'Y' : 'y';
        case 0x16: return (shift ^ caps_lock) ? 'U' : 'u';
        case 0x17: return (shift ^ caps_lock) ? 'I' : 'i';
        case 0x18: return (shift ^ caps_lock) ? 'O' : 'o';
        case 0x19: return (shift ^ caps_lock) ? 'P' : 'p';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x1E: return (shift ^ caps_lock) ? 'A' : 'a';
        case 0x1F: return (shift ^ caps_lock) ? 'S' : 's';
        case 0x20: return (shift ^ caps_lock) ? 'D' : 'd';
        case 0x21: return (shift ^ caps_lock) ? 'F' : 'f';
        case 0x22: return (shift ^ caps_lock) ? 'G' : 'g';
        case 0x23: return (shift ^ caps_lock) ? 'H' : 'h';
        case 0x24: return (shift ^ caps_lock) ? 'J' : 'j';
        case 0x25: return (shift ^ caps_lock) ? 'K' : 'k';
        case 0x26: return (shift ^ caps_lock) ? 'L' : 'l';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x2C: return (shift ^ caps_lock) ? 'Z' : 'z';
        case 0x2D: return (shift ^ caps_lock) ? 'X' : 'x';
        case 0x2E: return (shift ^ caps_lock) ? 'C' : 'c';
        case 0x2F: return (shift ^ caps_lock) ? 'V' : 'v';
        case 0x30: return (shift ^ caps_lock) ? 'B' : 'b';
        case 0x31: return (shift ^ caps_lock) ? 'N' : 'n';
        case 0x32: return (shift ^ caps_lock) ? 'M' : 'm';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        default: return 0;
    }
}

bool HandleModifierKey(uint8_t scancode, KeyboardState& kb) {
    bool released = (scancode & 0x80) != 0;
    uint8_t keycode = scancode & 0x7F;
    switch (keycode) {
        case 0x2A:
            kb.left_shift = !released;
            return true;
        case 0x36:
            kb.right_shift = !released;
            return true;
        case 0x1D:
            kb.left_ctrl = !released;
            return true;
        case 0x3A:
            if (!released) {
                kb.caps_lock = !kb.caps_lock;
            }
            return true;
        default:
            return false;
    }
}

bool IsShiftPressed(const KeyboardState& kb) {
    return kb.left_shift || kb.right_shift;
}

bool IsPrintableAscii(char c) {
    return c >= 0x20 && c <= 0x7e;
}

bool IsCtrlPressed(const KeyboardState& kb) {
    return kb.left_ctrl || kb.right_ctrl;
}

ShellPair* FindPair(ShellPair* pairs, int count, const char* key) {
    for (int i = 0; i < count; ++i) {
        if (pairs[i].used && StrEqual(pairs[i].key, key)) {
            return &pairs[i];
        }
    }
    return nullptr;
}

ShellPair* EnsurePair(ShellPair* pairs, int count, const char* key) {
    ShellPair* existing = FindPair(pairs, count, key);
    if (existing != nullptr) {
        return existing;
    }
    for (int i = 0; i < count; ++i) {
        if (!pairs[i].used) {
            pairs[i].used = true;
            CopyString(pairs[i].key, key, sizeof(pairs[i].key));
            pairs[i].value[0] = '\0';
            return &pairs[i];
        }
    }
    return nullptr;
}

void PrintPairs(const char* label, ShellPair* pairs, int count) {
    console->PrintLine(label);
    for (int i = 0; i < count; ++i) {
        if (pairs[i].used) {
            console->Print("  ");
            console->Print(pairs[i].key);
            console->Print("=");
            console->PrintLine(pairs[i].value);
        }
    }
}

void InitializeDirectories() {
    if (g_dirs_initialized) {
        return;
    }
    g_dirs_initialized = true;
    g_dirs[0].used = true;
    CopyString(g_dirs[0].path, "/", sizeof(g_dirs[0].path));
    for (int i = 1; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        g_dirs[i].used = false;
        g_dirs[i].path[0] = '\0';
    }
}

bool IsAbsolutePath(const char* path) {
    return path[0] == '/';
}

void BuildJoinedPath(const char* cwd, const char* path, char* out, int out_len) {
    if (out_len <= 0) {
        return;
    }
    if (IsAbsolutePath(path)) {
        CopyString(out, path, out_len);
        return;
    }
    if (StrEqual(cwd, "/")) {
        out[0] = '/';
        out[1] = '\0';
        CopyString(out + 1, path, out_len - 1);
        return;
    }
    CopyString(out, cwd, out_len);
    const int n = StrLength(out);
    if (n + 1 < out_len) {
        out[n] = '/';
        out[n + 1] = '\0';
        CopyString(out + n + 1, path, out_len - n - 1);
    }
}

bool ResolvePath(const char* cwd, const char* path, char* out, int out_len) {
    char joined[128];
    BuildJoinedPath(cwd, path, joined, sizeof(joined));

    char segments[16][32];
    int segment_count = 0;
    int i = 0;
    while (joined[i] != '\0') {
        while (joined[i] == '/') {
            ++i;
        }
        if (joined[i] == '\0') {
            break;
        }
        char seg[32];
        int w = 0;
        while (joined[i] != '\0' && joined[i] != '/') {
            if (w + 1 >= static_cast<int>(sizeof(seg))) {
                return false;
            }
            seg[w++] = joined[i];
            ++i;
        }
        seg[w] = '\0';

        if (StrEqual(seg, ".") || seg[0] == '\0') {
            continue;
        }
        if (StrEqual(seg, "..")) {
            if (segment_count > 0) {
                --segment_count;
            }
            continue;
        }
        if (segment_count >= static_cast<int>(sizeof(segments) / sizeof(segments[0]))) {
            return false;
        }
        CopyString(segments[segment_count], seg, sizeof(segments[segment_count]));
        ++segment_count;
    }

    out[0] = '/';
    out[1] = '\0';
    for (int s = 0; s < segment_count; ++s) {
        int n = StrLength(out);
        int seg_len = StrLength(segments[s]);
        const bool need_slash = (n > 1);
        const int required = n + (need_slash ? 1 : 0) + seg_len + 1;
        if (required > out_len) {
            return false;
        }
        if (need_slash) {
            out[n++] = '/';
            out[n] = '\0';
        }
        for (int j = 0; j < seg_len; ++j) {
            out[n++] = segments[s][j];
        }
        out[n] = '\0';
    }
    return true;
}

bool DirectoryExists(const char* path) {
    InitializeDirectories();
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (g_dirs[i].used && StrEqual(g_dirs[i].path, path)) {
            return true;
        }
    }
    return false;
}

bool GetParentPath(const char* path, char* out, int out_len) {
    if (!IsAbsolutePath(path)) {
        return false;
    }
    int len = StrLength(path);
    if (len <= 1) {
        CopyString(out, "/", out_len);
        return true;
    }
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') {
        --slash;
    }
    if (slash <= 0) {
        CopyString(out, "/", out_len);
        return true;
    }
    if (slash == 0) {
        CopyString(out, "/", out_len);
        return true;
    }
    if (slash + 1 > out_len) {
        return false;
    }
    for (int i = 0; i < slash; ++i) {
        out[i] = path[i];
    }
    out[slash] = '\0';
    return true;
}

bool CreateDirectory(const char* path) {
    InitializeDirectories();
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used) {
            g_dirs[i].used = true;
            CopyString(g_dirs[i].path, path, sizeof(g_dirs[i].path));
            return true;
        }
    }
    return false;
}

const ShellFile* FindShellFileByAbsPath(const char* abs_path) {
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (g_files[i].used && StrEqual(g_files[i].path, abs_path)) {
            return &g_files[i];
        }
    }
    return nullptr;
}

ShellFile* FindShellFileByAbsPathMutable(const char* abs_path) {
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (g_files[i].used && StrEqual(g_files[i].path, abs_path)) {
            return &g_files[i];
        }
    }
    return nullptr;
}

ShellFile* CreateShellFile(const char* abs_path) {
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            g_files[i].used = true;
            CopyString(g_files[i].path, abs_path, sizeof(g_files[i].path));
            g_files[i].size = 0;
            return &g_files[i];
        }
    }
    return nullptr;
}

bool ResolveFilePath(const char* cwd, const char* input, char* out, int out_len) {
    if (!ResolvePath(cwd, input, out, out_len)) {
        return false;
    }
    if (StrEqual(out, "/")) {
        return false;
    }
    char parent[96];
    if (!GetParentPath(out, parent, sizeof(parent))) {
        return false;
    }
    if (!DirectoryExists(parent)) {
        return false;
    }
    return true;
}

ShellDir* FindDirectoryMutable(const char* path) {
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (g_dirs[i].used && StrEqual(g_dirs[i].path, path)) {
            return &g_dirs[i];
        }
    }
    return nullptr;
}

bool IsPathSameOrChild(const char* path, const char* base) {
    if (StrEqual(base, "/")) {
        return true;
    }
    if (!StrStartsWith(path, base)) {
        return false;
    }
    int n = StrLength(base);
    return path[n] == '\0' || path[n] == '/';
}

bool IsDirectoryEmpty(const char* path) {
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used) {
            continue;
        }
        if (StrEqual(g_dirs[i].path, path)) {
            continue;
        }
        char parent[96];
        if (!GetParentPath(g_dirs[i].path, parent, sizeof(parent))) {
            continue;
        }
        if (StrEqual(parent, path)) {
            return false;
        }
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            continue;
        }
        char parent[96];
        if (!GetParentPath(g_files[i].path, parent, sizeof(parent))) {
            continue;
        }
        if (StrEqual(parent, path)) {
            return false;
        }
    }
    return true;
}

bool BuildMovedPath(const char* current, const char* src_prefix, const char* dst_prefix, char* out, int out_len) {
    if (!IsPathSameOrChild(current, src_prefix)) {
        return false;
    }
    int dst_len = StrLength(dst_prefix);
    int src_len = StrLength(src_prefix);
    const char* suffix = current + src_len;
    if (dst_len + StrLength(suffix) + 1 > out_len) {
        return false;
    }
    CopyString(out, dst_prefix, out_len);
    int n = StrLength(out);
    int j = 0;
    while (suffix[j] != '\0' && n + 1 < out_len) {
        out[n++] = suffix[j++];
    }
    out[n] = '\0';
    return true;
}

bool DirectoryExistsOutsideMove(const char* path, const char* src_prefix) {
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used) {
            continue;
        }
        if (IsPathSameOrChild(g_dirs[i].path, src_prefix)) {
            continue;
        }
        if (StrEqual(g_dirs[i].path, path)) {
            return true;
        }
    }
    return false;
}

bool ShellFileExistsOutsideMove(const char* path, const char* src_prefix) {
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            continue;
        }
        if (IsPathSameOrChild(g_files[i].path, src_prefix)) {
            continue;
        }
        if (StrEqual(g_files[i].path, path)) {
            return true;
        }
    }
    return false;
}

void GetBaseName(const char* path, char* out, int out_len) {
    int len = StrLength(path);
    if (len == 0) {
        out[0] = '\0';
        return;
    }
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') {
        --slash;
    }
    int start = (path[slash] == '/') ? slash + 1 : 0;
    CopyString(out, path + start, out_len);
}

void BuildBootFileAbsolutePath(const char* file_name, char* out, int out_len) {
    if (out_len <= 0) {
        return;
    }
    if (file_name[0] == '/') {
        CopyString(out, file_name, out_len);
        return;
    }
    out[0] = '/';
    out[1] = '\0';
    CopyString(out + 1, file_name, out_len - 1);
}

const BootFileEntry* FindBootFileByName(const char* name);

const ShellFile* FindShellFileByPath(const char* cwd, const char* input_path) {
    char resolved[96];
    if (!ResolvePath(cwd, input_path, resolved, sizeof(resolved))) {
        return nullptr;
    }
    if (StrEqual(resolved, "/")) {
        return nullptr;
    }
    return FindShellFileByAbsPath(resolved);
}

const BootFileEntry* FindBootFileByPath(const char* cwd, const char* input_path) {
    if (g_boot_info == nullptr || g_boot_info->boot_fs == nullptr) {
        return nullptr;
    }
    char resolved[96];
    if (!ResolvePath(cwd, input_path, resolved, sizeof(resolved))) {
        return nullptr;
    }
    if (StrEqual(resolved, "/")) {
        return nullptr;
    }
    const char* key = resolved;
    if (key[0] == '/') {
        ++key;
    }
    return FindBootFileByName(key);
}

bool ResolveAlias(const char* input, char* output, int output_len) {
    int pos = 0;
    char cmd[32];
    if (!NextToken(input, &pos, cmd, sizeof(cmd))) {
        output[0] = '\0';
        return false;
    }
    ShellPair* alias = FindPair(g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])), cmd);
    if (alias == nullptr) {
        CopyString(output, input, output_len);
        return false;
    }

    const char* tail = RestOfLine(input, pos);
    char merged[128];
    CopyString(merged, alias->value, sizeof(merged));
    if (tail[0] != '\0') {
        int n = StrLength(merged);
        if (n + 1 < static_cast<int>(sizeof(merged)) - 1) {
            merged[n++] = ' ';
            merged[n] = '\0';
            int j = 0;
            while (tail[j] != '\0' && n + 1 < static_cast<int>(sizeof(merged))) {
                merged[n++] = tail[j++];
            }
            merged[n] = '\0';
        }
    }
    CopyString(output, merged, output_len);
    return true;
}

void PrintBootFile(const BootFileEntry* file) {
    for (uint64_t i = 0; i < file->size; ++i) {
        char c = static_cast<char>(file->data[i]);
        if (c == '\r') {
            continue;
        }
        if (c == '\n' || IsPrintableAscii(c)) {
            char s[2] = {c, '\0'};
            console->Print(s);
        } else {
            console->Print(".");
        }
    }
    if (file->size == 0 || file->data[file->size - 1] != '\n') {
        console->Print("\n");
    }
}

char WaitPagerKey() {
    while (1) {
        __asm__ volatile("cli");
        if (main_queue == nullptr || main_queue->Count() == 0) {
            __asm__ volatile("sti\n\thlt");
            continue;
        }
        Message msg;
        main_queue->Pop(msg);
        __asm__ volatile("sti");

        if (msg.type == Message::Type::kInterruptMouse) {
            if (msg.pointer_mode == Message::PointerMode::kAbsolute) {
                mouse_cursor->SetPosition(msg.x, msg.y);
            } else {
                mouse_cursor->Move(msg.dx, msg.dy);
            }
            layer_manager->Draw();
            continue;
        }
        if (msg.type != Message::Type::kInterruptKeyboard) {
            continue;
        }
        if ((msg.keycode & 0x80) != 0 || msg.keycode == 0xE0) {
            continue;
        }
        const uint8_t key = msg.keycode & 0x7F;
        if (key == 0x1C || key == 0x39) { // Enter / Space
            return 'c';
        }
        if (key == 0x10) { // q
            return 'q';
        }
    }
}

void PrintBootFilePaged(const BootFileEntry* file, bool numbered) {
    int line = 1;
    int shown_lines = 0;
    if (numbered) {
        console->PrintDec(line++);
        console->Print(": ");
    }

    for (uint64_t i = 0; i < file->size; ++i) {
        char c = static_cast<char>(file->data[i]);
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            console->Print("\n");
            ++shown_lines;
            if (shown_lines >= console->Rows() - 2) {
                console->Print("-- more -- (Enter/Space/q)");
                layer_manager->Draw();
                char k = WaitPagerKey();
                console->Print("\n");
                if (k == 'q') {
                    return;
                }
                shown_lines = 0;
            }
            if (numbered && i + 1 < file->size) {
                console->PrintDec(line++);
                console->Print(": ");
            }
            continue;
        }

        if (IsPrintableAscii(c)) {
            char s[2] = {c, '\0'};
            console->Print(s);
        } else {
            console->Print(".");
        }
    }
    console->Print("\n");
}

void PrintBootFileNumbered(const BootFileEntry* file) {
    int line = 1;
    console->PrintDec(line);
    console->Print(": ");
    ++line;
    for (uint64_t i = 0; i < file->size; ++i) {
        char c = static_cast<char>(file->data[i]);
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            console->Print("\n");
            if (i + 1 < file->size) {
                console->PrintDec(line);
                console->Print(": ");
                ++line;
            }
            continue;
        }
        if (IsPrintableAscii(c)) {
            char s[2] = {c, '\0'};
            console->Print(s);
        } else {
            console->Print(".");
        }
    }
    console->Print("\n");
}

const BootFileEntry* FindBootFileByName(const char* name) {
    if (g_boot_info == nullptr || g_boot_info->boot_fs == nullptr) {
        return nullptr;
    }
    const BootFileSystem* fs = g_boot_info->boot_fs;
    for (uint32_t i = 0; i < fs->file_count; ++i) {
        if (StrEqual(fs->files[i].name, name)) {
            return &fs->files[i];
        }
    }
    return nullptr;
}

void PrintBootFileStat(const BootFileEntry* file) {
    uint64_t lines = 0;
    bool has_trailing_newline = false;
    for (uint64_t i = 0; i < file->size; ++i) {
        const char c = static_cast<char>(file->data[i]);
        if (c == '\n') {
            ++lines;
        }
    }
    if (file->size > 0 && static_cast<char>(file->data[file->size - 1]) == '\n') {
        has_trailing_newline = true;
    }

    console->Print("name: ");
    console->PrintLine(file->name);
    console->Print("size: ");
    console->PrintDec(static_cast<int64_t>(file->size));
    console->PrintLine(" B");
    console->Print("lines: ");
    if (file->size == 0) {
        console->PrintDec(0);
    } else {
        console->PrintDec(static_cast<int64_t>(lines + (has_trailing_newline ? 0 : 1)));
    }
    console->Print("\n");
}

void PrintShellFileStat(const ShellFile* file) {
    uint64_t lines = 0;
    bool has_trailing_newline = false;
    for (uint64_t i = 0; i < file->size; ++i) {
        const char c = static_cast<char>(file->data[i]);
        if (c == '\n') {
            ++lines;
        }
    }
    if (file->size > 0 && static_cast<char>(file->data[file->size - 1]) == '\n') {
        has_trailing_newline = true;
    }

    console->Print("name: ");
    console->PrintLine(file->path);
    console->Print("size: ");
    console->PrintDec(static_cast<int64_t>(file->size));
    console->PrintLine(" B");
    console->Print("lines: ");
    if (file->size == 0) {
        console->PrintDec(0);
    } else {
        console->PrintDec(static_cast<int64_t>(lines + (has_trailing_newline ? 0 : 1)));
    }
    console->Print("\n");
}

void PrintPrompt() {
    console->Print("os:");
    console->Print(g_cwd);
    console->Print("> ");
}

void Reboot() {
    __asm__ volatile("cli");
    for (int i = 0; i < 100000; ++i) {
        __asm__ volatile("" ::: "memory");
    }
    Out8(0x64, 0xFE);
    while (1) {
        __asm__ volatile("hlt");
    }
}

int ParseInt(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return 0;
    }
    int sign = 1;
    int i = 0;
    if (s[0] == '-') {
        sign = -1;
        i = 1;
    }
    int value = 0;
    for (; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            break;
        }
        value = value * 10 + (s[i] - '0');
    }
    return value * sign;
}

#include "shell/cmd_fs.cpp"

#include "shell/cmd_core.cpp"

#include "shell/cmd_dispatch.cpp"

#include "shell/cmd_xhci.cpp"

void ExecuteCommand(const char* command) {
    if (command[0] == '\0') {
        return;
    }

    char resolved[128];
    ResolveAlias(command, resolved, sizeof(resolved));
    command = resolved;

    int pos = 0;
    char cmd[32];
    if (!NextToken(command, &pos, cmd, sizeof(cmd))) {
        return;
    }
    const char* rest = RestOfLine(command, pos);

    if (ExecuteXHCICommand(cmd, command, &pos)) {
        return;
    }

    if (DispatchShellCommand(cmd, command, rest, &pos)) {
        return;
    }

    console->Print("unknown command: ");
    console->PrintLine(command);
}

void PrintHistory(char history[][128], int history_count) {
    for (int i = 0; i < history_count; ++i) {
        console->PrintDec(i + 1);
        console->Print(": ");
        console->PrintLine(history[i]);
    }
}
}

// C++標準ライブラリ（<new>）が存在しないため、配置new（Placement new）を自作する
void* operator new(size_t size, void* buf) {
    return buf;
}

// OSのメモリ管理機能を利用した、待望の真の「動的メモリ確保」
void* operator new(size_t size) {
    if (memory_manager == nullptr) {
        while (1) {
            __asm__ volatile("cli\n\thlt");
        }
    }

    const size_t total_size = size + sizeof(AllocationHeader);
    size_t num_pages = (total_size + kPageSize - 1) / kPageSize;
    uint64_t addr = memory_manager->Allocate(num_pages);
    if (addr == 0) {
        while (1) {
            __asm__ volatile("cli\n\thlt");
        }
    }

    auto* header = reinterpret_cast<AllocationHeader*>(addr);
    header->magic = kAllocationMagic;
    header->num_pages = num_pages;
    return reinterpret_cast<void*>(addr + sizeof(AllocationHeader));
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void* obj) noexcept {
    if (obj != nullptr && memory_manager != nullptr) {
        uint64_t obj_addr = reinterpret_cast<uint64_t>(obj);
        auto* header = reinterpret_cast<AllocationHeader*>(obj_addr - sizeof(AllocationHeader));
        if (header->magic == kAllocationMagic) {
            memory_manager->Free(reinterpret_cast<uint64_t>(header), header->num_pages);
        }
    }
}

void operator delete(void* obj, size_t size) noexcept {
    (void)size;
    operator delete(obj);
}

void operator delete[](void* obj) noexcept {
    operator delete(obj);
}

void operator delete[](void* obj, size_t size) noexcept {
    operator delete(obj, size);
}

// コンソールの実体を配置するバッファ（動的確保がまだできないため、配置newのような形で使う）
char console_buf[sizeof(Console)];
Console* console;

// マウスカーソルの実体を配置するバッファ
char mouse_cursor_buf[sizeof(MouseCursor)];
MouseCursor* mouse_cursor;

// メインキューとそのバッファ
char main_queue_buf[sizeof(ArrayQueue<Message, 256>)];
ArrayQueue<Message, 256>* main_queue;

// MemoryManagerの実体を配置するバッファ
char memory_manager_buf[sizeof(MemoryManager)];

// LayerManagerのグローバル変数
char layer_manager_buf[sizeof(LayerManager)];
LayerManager* layer_manager;

// カーネルの真のエントリポイント（UEFIシステムからは切り離されている）
// ブートローダー(main.efi)からポインタ経由で呼び出されるため、C言語の呼び出し規約を強制する
extern "C" void KernelMain(const struct BootInfo* boot_info) {
    const struct FrameBufferConfig* frame_buffer_config = boot_info->frame_buffer_config;
    g_boot_info = boot_info;

    // 0. IDT設定中に割り込みが来るとOSが吹き飛ぶ（トリプルフォールト）ため、必ず最初に割り込みを禁止(cli)する
    __asm__ volatile("cli");

    // 1. 最重要！割り込みとメモリ保護の基礎(GDT/IDT)を設定し、CPUをカーネルの支配下に置く
    InitializeGDT();
    InitializeIDT();

    // IDTに先ほど作った「マウス用割り込みハンドラ」を登録する
    InitializeInterruptHandlers();

    // 先にキューやメモリ関連を立ち上げる
    main_queue = new(main_queue_buf) ArrayQueue<Message, 256>();

    // 物理メモリの管理機構を立ち上げる
    memory_manager = new(memory_manager_buf) MemoryManager();
    memory_manager->Initialize(boot_info);

    // ★★★ 仮想メモリの初期化と適用 (ページング) ★★★
    // CR3が切り替わるとメモリのアドレス解釈が全て変わるため、即座にIDTも再ロードして
    // 割り込みハンドラへのアドレス解決がOSのページテーブル経由で正しく行われるようにする
    extern InterruptDescriptor idt[256];
    InitializePaging();
    LoadIDT(sizeof(idt) - 1, reinterpret_cast<uint64_t>(&idt[0]));

    // 2. GUI描画を総括する LayerManager の初期化
    layer_manager = new(layer_manager_buf) LayerManager(*frame_buffer_config);

    // 3. 背景用ウィンドウ（画面全体と同じサイズ）の作成とレイヤー登録
    Window* bg_window = new Window(
        frame_buffer_config->horizontal_resolution,
        frame_buffer_config->vertical_resolution
    );
    // 背景を黒で塗りつぶす
    bg_window->FillRectangle(0, 0, bg_window->Width(), bg_window->Height(), {0, 0, 0});
    
    Layer* bg_layer = layer_manager->NewLayer();
    bg_layer->SetWindow(bg_window).Move(0, 0);
    layer_manager->UpDown(bg_layer, 0); // 最背面(Z=0)に設定

    // 4. コンソールの初期化（描画先を背景ウィンドウのキャンバスに指定）
    console = new(console_buf) Console(bg_window, 
                                        255, 255, 255, // FG (White)
                                        0, 0, 0);      // BG (Black)
    console->Print("Initializing LayerManager...\n");

    // ★★★ 真の「動的メモリ確保 (new)」のテスト ★★★
    console->Print("Testing dynamic new : ");
    uint64_t* test_arr = new uint64_t[3];
    if (test_arr != nullptr) {
        test_arr[0] = 0xAA;
        test_arr[1] = 0xBB;
        console->Print("Success! (Addr: 0x");
        console->PrintHex(reinterpret_cast<uint64_t>(test_arr), 8);
        console->PrintLine(")");
        delete[] test_arr;
    } else {
        console->PrintLine("FAILED (Returned nullptr)");
    }

    console->PrintLine("Welcome to Native OS (C++ Edition)!");
    console->Print("Booting from ELF format...\n\n");
    console->Print("System is ready. Current MapKey is captured.\n\n");

    // マウスカーソルの初期化とレイヤーの登録
    console->Print("Drawing mouse cursor layer...\n");
    mouse_cursor = new(mouse_cursor_buf) MouseCursor(
        frame_buffer_config->horizontal_resolution / 2,
        frame_buffer_config->vertical_resolution / 2,
        layer_manager
    );

    // 最初に1度だけ画面全体を合成描画する
    layer_manager->Draw();

    console->Print("Scanning PCI bus...\n");
    InitializePCI();
    const auto& xhci = GetXHCIControllerInfo();
    if (xhci.found) {
        console->Print("xHCI found at ");
        console->PrintDec(xhci.address.bus);
        console->Print(":");
        console->PrintDec(xhci.address.device);
        console->Print(".");
        console->PrintDec(xhci.address.function);
        console->Print(" (vendor=0x");
        console->PrintHex(xhci.vendor_id, 4);
        console->Print(", device=0x");
        console->PrintHex(xhci.device_id, 4);
        console->Print(", mmio=0x");
        console->PrintHex(xhci.mmio_base, 16);
        console->Print(")\n");
        if (ProbeXHCIController(xhci, &g_xhci_caps) && g_xhci_caps.valid) {
            console->Print("xHCI caps: ver=0x");
            console->PrintHex(g_xhci_caps.hci_version, 4);
            console->Print(" caplen=0x");
            console->PrintHex(g_xhci_caps.cap_length, 2);
            console->Print(" slots=");
            console->PrintDec(g_xhci_caps.max_slots);
            console->Print(" ports=");
            console->PrintDec(g_xhci_caps.max_ports);
            console->Print(" db=0x");
            console->PrintHex(g_xhci_caps.db_off, 8);
            console->Print(" rts=0x");
            console->PrintHex(g_xhci_caps.rts_off, 8);
            console->Print("\n");

            if (g_boot_mouse_auto_enabled) {
                ResetHIDDecodeLearning();
                if (StartXHCIAutoMouse(kAutoStartHIDLen, kAutoStartHIDMps, kAutoStartHIDInterval)) {
                    console->Print("xHCI HID auto-start: ok (slot=");
                    console->PrintDec(g_xhci_hid_auto_slot);
                    console->Print(", len=");
                    console->PrintDec(g_xhci_hid_auto_len);
                    console->Print(")\n");
                } else {
                    console->PrintLine("xHCI HID auto-start: failed");
                }
            }
        }
    } else {
        console->Print("xHCI not found.\n");
    }

    // 5. 本格的なハードウェア割り込みを受け取るための環境構築
    console->Print("Initializing Interrupt Controller (PIC)...\n");
    InitializePIC(0x20); // IRQ0〜15 を IDTの 0x20〜0x2F (32〜47) に割り当てる

    console->Print("Initializing PS/2 Mouse...\n");
    InitializePS2Mouse();

    console->Print("Initializing Local APIC...\n");
    InitializeLocalAPIC();

    console->Print("Initializing LAPIC timer...\n");
    InitializeLAPICTimer();

    console->Print("Waiting for hardware interrupts (Keyboard/Mouse/LAPIC Timer)...\n");
    PrintPrompt();

    // 全ての初期化プロセスが終わった時点で、溜まったコンソール出力をVRAMへ全画面描画（反映）する
    layer_manager->Draw();

    // PICのマスクを解除して、実際に信号がCPUに飛んでくるようにする
    UnmaskIRQ(1);  // PS/2 Keyboard
    UnmaskIRQ(2);  // Slave PIC Cascade
    UnmaskIRQ(12); // PS/2 Mouse

    // sti: Set Interrupt Flag 命令を実行し、CPU全体として割り込みを「受ける」状態にする
    __asm__ volatile("sti");

    // OSのメインループ（イベントループ）
    KeyboardState keyboard_state;
    keyboard_state.left_shift = false;
    keyboard_state.right_shift = false;
    keyboard_state.caps_lock = false;
    keyboard_state.left_ctrl = false;
    keyboard_state.right_ctrl = false;
    char command_buffer[128];
    int command_len = 0;
    int cursor_pos = 0;
    command_buffer[0] = '\0';
    char command_history[16][128];
    int history_count = 0;
    int history_nav = -1;   // -1 = browsing off, 0..history_count-1 = selected history row
    char draft_buffer[128];
    draft_buffer[0] = '\0';
    bool e0_prefix = false;
    bool key_down[128];
    for (int i = 0; i < 128; ++i) {
        key_down[i] = false;
    }
    int input_row = console->CursorRow();
    int input_col = console->CursorColumn();
    int rendered_len = 0;
    int selection_anchor = -1;
    int selection_end = -1;
    bool selecting_with_mouse = false;
    auto RefreshConsole = [&]() {
        layer_manager->Draw(0, 0, console->PixelWidth(), console->PixelHeight());
    };
    auto RefreshInputLine = [&]() {
        int x = Console::kMarginX + input_col * Console::kCellWidth;
        int y = Console::kMarginY + input_row * Console::kCellHeight;
        int w = (console->Columns() - input_col) * Console::kCellWidth;
        int h = Console::kCellHeight;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        layer_manager->Draw(x, y, w, h);
    };
    auto EnsureLiveConsole = [&]() {
        if (console->IsScrolled()) {
            console->ResetScroll();
            RefreshConsole();
        }
    };

    auto HasSelection = [&]() {
        return selection_anchor >= 0 && selection_end >= 0 && selection_anchor != selection_end;
    };
    auto SelectionStart = [&]() {
        return (selection_anchor < selection_end) ? selection_anchor : selection_end;
    };
    auto SelectionEnd = [&]() {
        return (selection_anchor > selection_end) ? selection_anchor : selection_end;
    };
    auto ClearSelection = [&]() {
        selection_anchor = -1;
        selection_end = -1;
        selecting_with_mouse = false;
    };

    auto RenderInputLine = [&]() {
        const int clear_len = console->Columns() - input_col - 1;
        console->SetCursorPosition(input_row, input_col);
        for (int i = 0; i < clear_len; ++i) {
            console->Print(" ");
        }
        console->SetCursorPosition(input_row, input_col);
        console->Print(command_buffer);
        if (HasSelection()) {
            const int sel_start = SelectionStart();
            const int sel_end = SelectionEnd();
            Window* win = console->RawWindow();
            for (int i = sel_start; i < sel_end; ++i) {
                const int col = input_col + i;
                if (col < input_col || col >= console->Columns()) {
                    continue;
                }
                const int px = Console::kMarginX + col * Console::kCellWidth;
                const int py = Console::kMarginY + input_row * Console::kCellHeight;
                win->FillRectangle(px, py, Console::kCellWidth, Console::kCellHeight, {255, 255, 255});
                char c = (i < command_len) ? command_buffer[i] : ' ';
                win->DrawCharScaled(px, py, c, {0, 0, 0}, Console::kFontScale);
            }
        }
        rendered_len = command_len;
        console->SetCursorPosition(input_row, input_col + cursor_pos);
    };

    auto MaxInputLen = [&]() {
        const int row_limit = console->Columns() - input_col - 1;
        if (row_limit <= 0) {
            return 0;
        }
        const int buf_limit = static_cast<int>(sizeof(command_buffer)) - 1;
        return (row_limit < buf_limit) ? row_limit : buf_limit;
    };

    auto ReplaceInputLine = [&](const char* text) {
        CopyString(command_buffer, text, static_cast<int>(sizeof(command_buffer)));
        command_len = StrLength(command_buffer);
        const int max_input_len = MaxInputLen();
        if (command_len > max_input_len) {
            command_len = max_input_len;
            command_buffer[command_len] = '\0';
        }
        cursor_pos = command_len;
        ClearSelection();
        RenderInputLine();
        RefreshInputLine();
    };

    auto DeleteSelection = [&]() -> bool {
        if (!HasSelection()) {
            return false;
        }
        int sel_start = SelectionStart();
        int sel_end = SelectionEnd();
        if (sel_start < 0) sel_start = 0;
        if (sel_end > command_len) sel_end = command_len;
        const int remove_len = sel_end - sel_start;
        if (remove_len <= 0) {
            ClearSelection();
            return false;
        }
        for (int i = sel_start; i + remove_len <= command_len; ++i) {
            command_buffer[i] = command_buffer[i + remove_len];
        }
        command_len -= remove_len;
        if (command_len < 0) command_len = 0;
        command_buffer[command_len] = '\0';
        cursor_pos = sel_start;
        ClearSelection();
        RenderInputLine();
        RefreshInputLine();
        return true;
    };

    auto BackspaceAtCursor = [&]() {
        if (DeleteSelection()) {
            return;
        }
        if (cursor_pos <= 0 || command_len <= 0) {
            return;
        }
        for (int i = cursor_pos - 1; i < command_len; ++i) {
            command_buffer[i] = command_buffer[i + 1];
        }
        --command_len;
        --cursor_pos;
        command_buffer[command_len] = '\0';
        RenderInputLine();
        RefreshInputLine();
    };

    auto DeleteAtCursor = [&]() {
        if (DeleteSelection()) {
            return;
        }
        if (command_len <= 0) {
            return;
        }
        if (cursor_pos < command_len) {
            for (int i = cursor_pos; i < command_len; ++i) {
                command_buffer[i] = command_buffer[i + 1];
            }
            --command_len;
            command_buffer[command_len] = '\0';
            RenderInputLine();
            RefreshInputLine();
            return;
        }
        // ユーザー体験優先: 行末でDeleteが押されたらBackspace相当で1文字消す
        if (cursor_pos == command_len) {
            BackspaceAtCursor();
        }
    };

    auto HandleTabCompletion = [&]() {
        if (command_len == 0 || cursor_pos != command_len) {
            return;
        }
        int token_start = cursor_pos;
        while (token_start > 0 && command_buffer[token_start - 1] != ' ') {
            --token_start;
        }
        char token[64];
        int tw = 0;
        for (int i = token_start; i < cursor_pos && tw + 1 < static_cast<int>(sizeof(token)); ++i) {
            token[tw++] = command_buffer[i];
        }
        token[tw] = '\0';

        const bool first_token = (token_start == 0);
        int match_count = 0;
        const char* single_match = nullptr;

        char candidates[128][64];
        auto AddCandidate = [&](const char* name) {
            for (int i = 0; i < match_count; ++i) {
                if (StrEqual(candidates[i], name)) {
                    return;
                }
            }
            if (match_count >= static_cast<int>(sizeof(candidates) / sizeof(candidates[0]))) {
                return;
            }
            CopyString(candidates[match_count], name, sizeof(candidates[match_count]));
            ++match_count;
        };

        if (first_token) {
            for (int i = 0; i < kBuiltInCommandCount; ++i) {
                if (StrStartsWith(kBuiltInCommands[i], token)) {
                    AddCandidate(kBuiltInCommands[i]);
                }
            }
        } else {
            if (ContainsChar(token, '/')) {
                return;
            }
            for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
                if (!g_dirs[i].used || StrEqual(g_dirs[i].path, "/")) {
                    continue;
                }
                char parent[96];
                if (!GetParentPath(g_dirs[i].path, parent, sizeof(parent)) || !StrEqual(parent, g_cwd)) {
                    continue;
                }
                char base[64];
                GetBaseName(g_dirs[i].path, base, sizeof(base));
                if (!StrStartsWith(base, token)) {
                    continue;
                }
                char dname[64];
                CopyString(dname, base, sizeof(dname));
                int n = StrLength(dname);
                if (n + 1 < static_cast<int>(sizeof(dname))) {
                    dname[n] = '/';
                    dname[n + 1] = '\0';
                }
                AddCandidate(dname);
            }
            for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
                if (!g_files[i].used) {
                    continue;
                }
                char parent[96];
                if (!GetParentPath(g_files[i].path, parent, sizeof(parent)) || !StrEqual(parent, g_cwd)) {
                    continue;
                }
                char base[64];
                GetBaseName(g_files[i].path, base, sizeof(base));
                if (StrStartsWith(base, token)) {
                    AddCandidate(base);
                }
            }
            if (g_boot_info != nullptr && g_boot_info->boot_fs != nullptr) {
                const BootFileSystem* fs = g_boot_info->boot_fs;
                for (uint32_t i = 0; i < fs->file_count; ++i) {
                    char abs_file_path[96];
                    BuildBootFileAbsolutePath(fs->files[i].name, abs_file_path, sizeof(abs_file_path));
                    if (FindShellFileByAbsPath(abs_file_path) != nullptr) {
                        continue;
                    }
                    char parent[96];
                    if (!GetParentPath(abs_file_path, parent, sizeof(parent)) || !StrEqual(parent, g_cwd)) {
                        continue;
                    }
                    char base[64];
                    GetBaseName(abs_file_path, base, sizeof(base));
                    if (StrStartsWith(base, token)) {
                        AddCandidate(base);
                    }
                }
            }
        }

        if (match_count == 0) {
            return;
        }
        if (match_count == 1) {
            single_match = candidates[0];
            char replaced[128];
            int w = 0;
            for (int i = 0; i < token_start && w + 1 < static_cast<int>(sizeof(replaced)); ++i) {
                replaced[w++] = command_buffer[i];
            }
            for (int i = 0; single_match[i] != '\0' && w + 1 < static_cast<int>(sizeof(replaced)); ++i) {
                replaced[w++] = single_match[i];
            }
            replaced[w] = '\0';
            ReplaceInputLine(replaced);
            return;
        }

        console->Print("\n");
        for (int i = 0; i < match_count; ++i) {
            console->Print(candidates[i]);
            console->Print(" ");
        }
        console->Print("\n");
        PrintPrompt();
        input_row = console->CursorRow();
        input_col = console->CursorColumn();
        rendered_len = 0;
        cursor_pos = command_len;
        RenderInputLine();
        RefreshInputLine();
    };

    auto BrowseHistoryUp = [&]() {
        if (history_count <= 0) {
            return;
        }
        if (history_nav == -1) {
            CopyString(draft_buffer, command_buffer, static_cast<int>(sizeof(draft_buffer)));
            history_nav = history_count - 1;
        } else if (history_nav > 0) {
            --history_nav;
        }
        ReplaceInputLine(command_history[history_nav]);
    };

    auto BrowseHistoryDown = [&]() {
        if (history_nav < 0) {
            return;
        }
        if (history_nav < history_count - 1) {
            ++history_nav;
            ReplaceInputLine(command_history[history_nav]);
        } else {
            history_nav = -1;
            ReplaceInputLine(draft_buffer);
        }
    };

    while (1) {
        // 処理すべきイベントがあるか、割り込みを禁止(cli)した上で安全にチェックする（競合対策）
        __asm__ volatile("cli");
        if (main_queue->Count() == 0) {
            __asm__ volatile("sti");
            if (g_xhci_hid_auto_enabled && g_xhci_hid_auto_slot > 0) {
                uint64_t now_tick = CurrentTick();
                if (now_tick != g_xhci_hid_last_poll_tick) {
                    g_xhci_hid_last_poll_tick = now_tick;
                    if (PollHIDAndApply(g_xhci_hid_auto_slot, g_xhci_hid_auto_len, false)) {
                        g_xhci_hid_auto_consecutive_failures = 0;
                        continue;
                    } else {
                        ++g_xhci_hid_auto_fail_count;
                        ++g_xhci_hid_auto_consecutive_failures;
                    }
                }

                const uint32_t kRecoverFailThreshold = 24;
                if (g_xhci_hid_auto_consecutive_failures >= kRecoverFailThreshold &&
                    now_tick >= g_xhci_hid_next_recover_tick) {
                    g_xhci_hid_next_recover_tick = now_tick + 120;  // retry after a short cool down
                    ResetHIDDecodeLearning();
                    if (StartXHCIAutoMouse(g_xhci_hid_auto_len, g_xhci_hid_auto_mps, g_xhci_hid_auto_interval)) {
                        ++g_xhci_hid_auto_recover_count;
                        g_xhci_hid_auto_consecutive_failures = 0;
                    }
                }
            }
            // キューが空ならばCPUを休止(hlt)させる
            __asm__ volatile("hlt");
            continue;
        }

        // キューにデータが入っていたら、メッセージを1つ取り出す
        Message msg;
        main_queue->Pop(msg);
        
        // 取り出し終わったら割り込みを再開する
        __asm__ volatile("sti");

        // 取り出したメッセージの種類ごとに重い処理（状態の更新）を行う
        switch (msg.type) {
            case Message::Type::kInterruptMouse:
                {
                const uint8_t prev_buttons = g_mouse_buttons_current;
                const uint8_t now_buttons = msg.buttons;
                const uint8_t pressed = static_cast<uint8_t>((~prev_buttons) & now_buttons);
                if ((pressed & 0x01) != 0) { ++g_mouse_left_press_count; }
                if ((pressed & 0x02) != 0) { ++g_mouse_right_press_count; }
                if ((pressed & 0x04) != 0) { ++g_mouse_middle_press_count; }
                g_mouse_buttons_current = now_buttons;

                if (msg.pointer_mode == Message::PointerMode::kAbsolute) {
                    g_last_absolute_mouse_tick = CurrentTick();
                    mouse_cursor->SetPosition(msg.x, msg.y);
                    const int click_col = (msg.x - Console::kMarginX) / Console::kCellWidth;
                    const int click_row = (msg.y - Console::kMarginY) / Console::kCellHeight;
                    if ((pressed & 0x01) != 0) {
                        selecting_with_mouse = true;
                        if (click_row == input_row && click_col >= input_col) {
                            int at = click_col - input_col;
                            if (at < 0) at = 0;
                            if (at > command_len) at = command_len;
                            selection_anchor = at;
                            selection_end = at;
                        } else {
                            ClearSelection();
                        }
                    }
                    if ((now_buttons & 0x01) != 0 && selecting_with_mouse &&
                        click_row == input_row && click_col >= input_col) {
                        int next_cursor = click_col - input_col;
                        if (next_cursor < 0) next_cursor = 0;
                        if (next_cursor > command_len) next_cursor = command_len;
                        selection_end = next_cursor;
                        if (next_cursor != cursor_pos) {
                            EnsureLiveConsole();
                            cursor_pos = next_cursor;
                            RenderInputLine();
                            RefreshInputLine();
                        }
                    }
                    if (((prev_buttons & 0x01) != 0) && ((now_buttons & 0x01) == 0)) {
                        selecting_with_mouse = false;
                    }
                    if ((pressed & 0x01) != 0 && !HasSelection()) {  // Left click
                        EnsureLiveConsole();
                        if (click_row == input_row && click_col >= input_col) {
                            int next_cursor = click_col - input_col;
                            if (next_cursor < 0) next_cursor = 0;
                            if (next_cursor > command_len) next_cursor = command_len;
                            cursor_pos = next_cursor;
                            RenderInputLine();
                            RefreshInputLine();
                        }
                    }
                } else {
                    if (g_xhci_hid_auto_enabled &&
                        (CurrentTick() - g_last_absolute_mouse_tick) < 1000) {
                        break;  // USB absolute pointer is active; ignore noisy PS/2 relative moves.
                    }
                    mouse_cursor->Move(msg.dx, msg.dy);
                }
                if (msg.wheel > 0) {
                    console->ScrollUp(msg.wheel * 3);
                    RefreshConsole();
                } else if (msg.wheel < 0) {
                    console->ScrollDown((-msg.wheel) * 3);
                    RefreshConsole();
                }
                }
                break;
            case Message::Type::kInterruptKeyboard:
                if (msg.keycode == 0xE0) {
                    e0_prefix = true;
                    break;
                }

                if (e0_prefix) {
                    uint8_t ext = msg.keycode;
                    e0_prefix = false;
                    if ((ext & 0x7F) == 0x1D) { // Right Ctrl
                        keyboard_state.right_ctrl = ((ext & 0x80) == 0);
                        break;
                    }
                    if ((ext & 0x80) != 0) {
                        break;
                    }
                    if ((ext & 0x7F) == 0x49) { // Page Up
                        console->ScrollUp(3);
                        RefreshConsole();
                    } else if ((ext & 0x7F) == 0x51) { // Page Down
                        console->ScrollDown(3);
                        RefreshConsole();
                    } else if ((ext & 0x7F) == 0x53) { // Delete
                        EnsureLiveConsole();
                        DeleteAtCursor();
                    } else if ((ext & 0x7F) == 0x4B) { // Arrow Left
                        EnsureLiveConsole();
                        if (cursor_pos > 0) {
                            ClearSelection();
                            --cursor_pos;
                            RenderInputLine();
                            RefreshInputLine();
                        }
                    } else if ((ext & 0x7F) == 0x4D) { // Arrow Right
                        EnsureLiveConsole();
                        if (cursor_pos < command_len) {
                            ClearSelection();
                            ++cursor_pos;
                            RenderInputLine();
                            RefreshInputLine();
                        }
                    } else if ((ext & 0x7F) == 0x47) { // Home
                        EnsureLiveConsole();
                        ClearSelection();
                        cursor_pos = 0;
                        RenderInputLine();
                        RefreshInputLine();
                    } else if ((ext & 0x7F) == 0x4F) { // End
                        EnsureLiveConsole();
                        ClearSelection();
                        cursor_pos = command_len;
                        RenderInputLine();
                        RefreshInputLine();
                    } else if ((ext & 0x7F) == 0x48) { // Arrow Up
                        EnsureLiveConsole();
                        BrowseHistoryUp();
                    } else if ((ext & 0x7F) == 0x50) { // Arrow Down
                        EnsureLiveConsole();
                        BrowseHistoryDown();
                    }
                    break;
                }

                if (HandleModifierKey(msg.keycode, keyboard_state)) {
                    break;
                }
                if ((msg.keycode & 0x80) != 0) {
                    uint8_t keyup = msg.keycode & 0x7F;
                    if (keyup < 128) {
                        key_down[keyup] = false;
                    }
                    break;
                }
                if ((msg.keycode & 0x80) == 0) {
                    const uint8_t key = msg.keycode & 0x7F;
                    if (key < 128 && !g_key_repeat_enabled && key_down[key]) {
                        break;
                    }
                    if (key < 128) {
                        key_down[key] = true;
                    }
                    if (IsCtrlPressed(keyboard_state)) {
                        if (key == 0x1E) { // Ctrl + A
                            EnsureLiveConsole();
                            ClearSelection();
                            cursor_pos = 0;
                            RenderInputLine();
                            RefreshInputLine();
                            break;
                        }
                        if (key == 0x12) { // Ctrl + E
                            EnsureLiveConsole();
                            ClearSelection();
                            cursor_pos = command_len;
                            RenderInputLine();
                            RefreshInputLine();
                            break;
                        }
                        if (key == 0x26) { // Ctrl + L
                            console->Clear();
                            PrintPrompt();
                            input_row = console->CursorRow();
                            input_col = console->CursorColumn();
                            rendered_len = 0;
                            command_len = 0;
                            cursor_pos = 0;
                            command_buffer[0] = '\0';
                            ClearSelection();
                            history_nav = -1;
                            draft_buffer[0] = '\0';
                            RenderInputLine();
                            RefreshInputLine();
                            break;
                        }
                    }
                    if (key == 0x47) { // Home (non-E0 fallback)
                        EnsureLiveConsole();
                        ClearSelection();
                        cursor_pos = 0;
                        RenderInputLine();
                        RefreshInputLine();
                        break;
                    }
                    if (key == 0x48) { // Arrow Up (non-E0 fallback)
                        EnsureLiveConsole();
                        BrowseHistoryUp();
                        break;
                    }
                    if (key == 0x4F) { // End (non-E0 fallback)
                        EnsureLiveConsole();
                        ClearSelection();
                        cursor_pos = command_len;
                        RenderInputLine();
                        RefreshInputLine();
                        break;
                    }
                    if (key == 0x50) { // Arrow Down (non-E0 fallback)
                        EnsureLiveConsole();
                        BrowseHistoryDown();
                        break;
                    }
                    if (key == 0x0E || key == 0x53 || key == 0x71) { // Backspace/Delete
                        EnsureLiveConsole();
                        if (key == 0x0E) {
                            BackspaceAtCursor();
                        } else {
                            DeleteAtCursor();
                        }
                        break;
                    }
                    if (key == 0x0F) { // Tab
                        EnsureLiveConsole();
                        HandleTabCompletion();
                        break;
                    }

                    char ch = KeycodeToAscii(key,
                                             IsShiftPressed(keyboard_state),
                                             keyboard_state.caps_lock);
                    if (ch != 0) {
                        EnsureLiveConsole();
                        bool full_refresh = false;
                        if (ch == '\n') {
                            console->SetCursorPosition(input_row, input_col + command_len);
                            console->Print("\n");
                            command_buffer[command_len] = '\0';
                            const bool is_history_cmd = StrEqual(command_buffer, "history");
                            const bool is_clear_history_cmd = StrEqual(command_buffer, "clearhistory");
                            if (command_len > 0) {
                                if (history_count < static_cast<int>(sizeof(command_history) / sizeof(command_history[0]))) {
                                    CopyString(command_history[history_count], command_buffer, 128);
                                    ++history_count;
                                } else {
                                    for (int i = 1; i < static_cast<int>(sizeof(command_history) / sizeof(command_history[0])); ++i) {
                                        CopyString(command_history[i - 1], command_history[i], 128);
                                    }
                                    CopyString(command_history[static_cast<int>(sizeof(command_history) / sizeof(command_history[0])) - 1],
                                               command_buffer, 128);
                                }
                            }
                            if (is_history_cmd) {
                                PrintHistory(command_history, history_count);
                            } else if (is_clear_history_cmd) {
                                history_count = 0;
                                console->PrintLine("history cleared");
                            } else {
                                ExecuteCommand(command_buffer);
                            }
                            command_len = 0;
                            cursor_pos = 0;
                            rendered_len = 0;
                            command_buffer[0] = '\0';
                            ClearSelection();
                            history_nav = -1;
                            draft_buffer[0] = '\0';
                            PrintPrompt();
                            input_row = console->CursorRow();
                            input_col = console->CursorColumn();
                            console->SetCursorPosition(input_row, input_col);
                            full_refresh = true;
                        } else if (IsPrintableAscii(ch)) {
                            DeleteSelection();
                            if (command_len < MaxInputLen()) {
                                for (int i = command_len; i > cursor_pos; --i) {
                                    command_buffer[i] = command_buffer[i - 1];
                                }
                                command_buffer[cursor_pos] = ch;
                                ++command_len;
                                ++cursor_pos;
                                command_buffer[command_len] = '\0';
                                RenderInputLine();
                            }
                        }
                        if (full_refresh) {
                            RefreshConsole();
                        } else {
                            RefreshInputLine();
                        }
                    }
                }
                break;
            default:
                break;
        }

    }
}
