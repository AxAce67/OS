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
#include "input/key_event.hpp"
#include "input/key_layout.hpp"
#include "input/hid_keyboard.hpp"
#include "boot_info.h"
#include "memory.hpp"
#include "paging.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "apic.hpp"
#include "timer.hpp"
#include "shell/commands.hpp"
#include "shell/context.hpp"
#include "shell/cmd_dispatch.hpp"
#include "shell/cmd_xhci.hpp"
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

ShellPair g_vars[16];
ShellPair g_aliases[16];
ShellDir g_dirs[32];
ShellFile g_files[64];
bool g_key_repeat_enabled = true;
bool g_jp_layout = true;
bool g_ime_enabled = false;
bool g_has_halfwidth_kana_font = false;
uint64_t g_keyboard_irq_count = 0;
uint8_t g_keyboard_last_raw = 0;
uint8_t g_keyboard_last_key = 0;
bool g_keyboard_last_extended = false;
bool g_keyboard_last_released = false;
const BootInfo* g_boot_info = nullptr;
bool g_dirs_initialized = false;
char g_cwd[96] = "/";
XHCICapabilityInfo g_xhci_caps = {};
uint8_t g_last_xhci_slot_id = 0;
bool g_xhci_hid_auto_enabled = false;
uint8_t g_xhci_hid_auto_slot = 0;
uint32_t g_xhci_hid_auto_len = 8;
bool g_xhci_hid_decode_keyboard = false;
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

    if (g_xhci_hid_decode_keyboard) {
        uint8_t key_scancodes[32];
        uint8_t key_count = 0;
        if (DecodeHIDBootKeyboardToSet1(rr.data, rr.data_length, key_scancodes, &key_count,
                                        static_cast<uint8_t>(sizeof(key_scancodes)))) {
            for (uint8_t i = 0; i < key_count; ++i) {
                EnqueueKeyboardScancode(key_scancodes[i]);
            }
            if (verbose) {
                console->Print("xhcihidpoll: keyboard bytes=");
                console->PrintDec(key_count);
                console->Print("\n");
            }
            return true;
        }
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
    g_xhci_hid_decode_keyboard = false;  // Auto path is used for absolute pointer polling.
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

bool HandleXHCIAutoPollOnIdle() {
    if (!g_xhci_hid_auto_enabled || g_xhci_hid_auto_slot == 0) {
        return false;
    }

    uint64_t now_tick = CurrentTick();
    if (now_tick != g_xhci_hid_last_poll_tick) {
        g_xhci_hid_last_poll_tick = now_tick;
        if (PollHIDAndApply(g_xhci_hid_auto_slot, g_xhci_hid_auto_len, false)) {
            g_xhci_hid_auto_consecutive_failures = 0;
            return true;
        }
        ++g_xhci_hid_auto_fail_count;
        ++g_xhci_hid_auto_consecutive_failures;
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
    return false;
}

bool IsPrintableAscii(char c) {
    return c >= 0x20 && c <= 0x7e;
}

bool HasHalfwidthKanaFont() {
    for (int ch = 0xA1; ch <= 0xDF; ++ch) {
        bool nonzero = false;
        for (int row = 0; row < 16; ++row) {
            if (kFont[ch][row] != 0) {
                nonzero = true;
                break;
            }
        }
        if (!nonzero) {
            return false;
        }
    }
    return true;
}

char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool IsRomajiVowel(char c) {
    return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o';
}

bool IsRomajiConsonant(char c) {
    return c >= 'a' && c <= 'z' && !IsRomajiVowel(c);
}

struct RomajiKanaEntry {
    const char* roma;
    uint8_t bytes[3];
    uint8_t len;
};

struct ImeCandidateEntry {
    const char* key;             // romaji source
    const char* candidates[4];   // display/insert text (single-byte font space)
    int count;
};

struct ImeUserCandidateEntry {
    bool used;
    char key[32];
    char candidates[4][32];
    int count;
};

struct ImeCandidateLearnEntry {
    bool used;
    char key[32];
    char cand[32];
    uint16_t score;
};

const RomajiKanaEntry kRomajiKanaTable[] = {
    {"kya", {0xB7, 0xAC, 0x00}, 2}, {"kyu", {0xB7, 0xAD, 0x00}, 2}, {"kyo", {0xB7, 0xAE, 0x00}, 2},
    {"sya", {0xBC, 0xAC, 0x00}, 2}, {"syu", {0xBC, 0xAD, 0x00}, 2}, {"syo", {0xBC, 0xAE, 0x00}, 2},
    {"sha", {0xBC, 0xAC, 0x00}, 2}, {"shu", {0xBC, 0xAD, 0x00}, 2}, {"sho", {0xBC, 0xAE, 0x00}, 2},
    {"tya", {0xC1, 0xAC, 0x00}, 2}, {"tyu", {0xC1, 0xAD, 0x00}, 2}, {"tyo", {0xC1, 0xAE, 0x00}, 2},
    {"cha", {0xC1, 0xAC, 0x00}, 2}, {"chu", {0xC1, 0xAD, 0x00}, 2}, {"cho", {0xC1, 0xAE, 0x00}, 2},
    {"nya", {0xC6, 0xAC, 0x00}, 2}, {"nyu", {0xC6, 0xAD, 0x00}, 2}, {"nyo", {0xC6, 0xAE, 0x00}, 2},
    {"hya", {0xCB, 0xAC, 0x00}, 2}, {"hyu", {0xCB, 0xAD, 0x00}, 2}, {"hyo", {0xCB, 0xAE, 0x00}, 2},
    {"mya", {0xD0, 0xAC, 0x00}, 2}, {"myu", {0xD0, 0xAD, 0x00}, 2}, {"myo", {0xD0, 0xAE, 0x00}, 2},
    {"rya", {0xD8, 0xAC, 0x00}, 2}, {"ryu", {0xD8, 0xAD, 0x00}, 2}, {"ryo", {0xD8, 0xAE, 0x00}, 2},
    {"gya", {0xB7, 0xDE, 0xAC}, 3}, {"gyu", {0xB7, 0xDE, 0xAD}, 3}, {"gyo", {0xB7, 0xDE, 0xAE}, 3},
    {"ja",  {0xBC, 0xDE, 0xAC}, 3}, {"ju",  {0xBC, 0xDE, 0xAD}, 3}, {"jo",  {0xBC, 0xDE, 0xAE}, 3},
    {"bya", {0xCB, 0xDE, 0xAC}, 3}, {"byu", {0xCB, 0xDE, 0xAD}, 3}, {"byo", {0xCB, 0xDE, 0xAE}, 3},
    {"pya", {0xCB, 0xDF, 0xAC}, 3}, {"pyu", {0xCB, 0xDF, 0xAD}, 3}, {"pyo", {0xCB, 0xDF, 0xAE}, 3},
    {"dya", {0xC1, 0xDE, 0xAC}, 3}, {"dyu", {0xC1, 0xDE, 0xAD}, 3}, {"dyo", {0xC1, 0xDE, 0xAE}, 3},
    {"sye", {0xBC, 0xAA, 0x00}, 2}, {"she", {0xBC, 0xAA, 0x00}, 2}, {"jye", {0xBC, 0xDE, 0xAA}, 3},
    {"je",  {0xBC, 0xDE, 0xAA}, 3}, {"che", {0xC1, 0xAA, 0x00}, 2},
    {"tsa", {0xC2, 0xA7, 0x00}, 2}, {"tsi", {0xC2, 0xA8, 0x00}, 2}, {"tse", {0xC2, 0xAA, 0x00}, 2},
    {"tso", {0xC2, 0xAB, 0x00}, 2},
    {"fa",  {0xCC, 0xA7, 0x00}, 2}, {"fi",  {0xCC, 0xA8, 0x00}, 2}, {"fe",  {0xCC, 0xAA, 0x00}, 2},
    {"fo",  {0xCC, 0xAB, 0x00}, 2}, {"fya", {0xCC, 0xAC, 0x00}, 2}, {"fyu", {0xCC, 0xAD, 0x00}, 2},
    {"fyo", {0xCC, 0xAE, 0x00}, 2}, {"la",  {0xA7, 0x00, 0x00}, 1}, {"li",  {0xA8, 0x00, 0x00}, 1},
    {"lu",  {0xA9, 0x00, 0x00}, 1}, {"le",  {0xAA, 0x00, 0x00}, 1}, {"lo",  {0xAB, 0x00, 0x00}, 1},
    {"xa",  {0xA7, 0x00, 0x00}, 1}, {"xi",  {0xA8, 0x00, 0x00}, 1}, {"xu",  {0xA9, 0x00, 0x00}, 1},
    {"xe",  {0xAA, 0x00, 0x00}, 1}, {"xo",  {0xAB, 0x00, 0x00}, 1}, {"xtsu",{0xAF, 0x00, 0x00}, 1},
    {"ltsu",{0xAF, 0x00, 0x00}, 1}, {"xya", {0xAC, 0x00, 0x00}, 1}, {"xyu", {0xAD, 0x00, 0x00}, 1},
    {"xyo", {0xAE, 0x00, 0x00}, 1}, {"lya", {0xAC, 0x00, 0x00}, 1}, {"lyu", {0xAD, 0x00, 0x00}, 1},
    {"lyo", {0xAE, 0x00, 0x00}, 1}, {"nn",  {0xDD, 0x00, 0x00}, 1}, {"a",   {0xB1, 0x00, 0x00}, 1},
    {"i",   {0xB2, 0x00, 0x00}, 1}, {"u",   {0xB3, 0x00, 0x00}, 1}, {"e",   {0xB4, 0x00, 0x00}, 1},
    {"o",   {0xB5, 0x00, 0x00}, 1}, {"ka",  {0xB6, 0x00, 0x00}, 1}, {"ki",  {0xB7, 0x00, 0x00}, 1},
    {"ku",  {0xB8, 0x00, 0x00}, 1}, {"ke",  {0xB9, 0x00, 0x00}, 1}, {"ko",  {0xBA, 0x00, 0x00}, 1},
    {"sa",  {0xBB, 0x00, 0x00}, 1}, {"shi", {0xBC, 0x00, 0x00}, 1}, {"si",  {0xBC, 0x00, 0x00}, 1},
    {"su",  {0xBD, 0x00, 0x00}, 1}, {"se",  {0xBE, 0x00, 0x00}, 1}, {"so",  {0xBF, 0x00, 0x00}, 1},
    {"ta",  {0xC0, 0x00, 0x00}, 1}, {"chi", {0xC1, 0x00, 0x00}, 1}, {"ti",  {0xC1, 0x00, 0x00}, 1},
    {"tsu", {0xC2, 0x00, 0x00}, 1}, {"tu",  {0xC2, 0x00, 0x00}, 1}, {"te",  {0xC3, 0x00, 0x00}, 1},
    {"to",  {0xC4, 0x00, 0x00}, 1}, {"na",  {0xC5, 0x00, 0x00}, 1}, {"ni",  {0xC6, 0x00, 0x00}, 1},
    {"nu",  {0xC7, 0x00, 0x00}, 1}, {"ne",  {0xC8, 0x00, 0x00}, 1}, {"no",  {0xC9, 0x00, 0x00}, 1},
    {"ha",  {0xCA, 0x00, 0x00}, 1}, {"hi",  {0xCB, 0x00, 0x00}, 1}, {"fu",  {0xCC, 0x00, 0x00}, 1},
    {"hu",  {0xCC, 0x00, 0x00}, 1}, {"he",  {0xCD, 0x00, 0x00}, 1}, {"ho",  {0xCE, 0x00, 0x00}, 1},
    {"ma",  {0xCF, 0x00, 0x00}, 1}, {"mi",  {0xD0, 0x00, 0x00}, 1}, {"mu",  {0xD1, 0x00, 0x00}, 1},
    {"me",  {0xD2, 0x00, 0x00}, 1}, {"mo",  {0xD3, 0x00, 0x00}, 1}, {"ya",  {0xD4, 0x00, 0x00}, 1},
    {"yu",  {0xD5, 0x00, 0x00}, 1}, {"yo",  {0xD6, 0x00, 0x00}, 1}, {"ra",  {0xD7, 0x00, 0x00}, 1},
    {"ri",  {0xD8, 0x00, 0x00}, 1}, {"ru",  {0xD9, 0x00, 0x00}, 1}, {"re",  {0xDA, 0x00, 0x00}, 1},
    {"ro",  {0xDB, 0x00, 0x00}, 1}, {"wa",  {0xDC, 0x00, 0x00}, 1}, {"wo",  {0xA6, 0x00, 0x00}, 1},
    {"ga",  {0xB6, 0xDE, 0x00}, 2}, {"gi",  {0xB7, 0xDE, 0x00}, 2}, {"gu",  {0xB8, 0xDE, 0x00}, 2},
    {"ge",  {0xB9, 0xDE, 0x00}, 2}, {"go",  {0xBA, 0xDE, 0x00}, 2}, {"za",  {0xBB, 0xDE, 0x00}, 2},
    {"ji",  {0xBC, 0xDE, 0x00}, 2}, {"zi",  {0xBC, 0xDE, 0x00}, 2}, {"zu",  {0xBD, 0xDE, 0x00}, 2},
    {"ze",  {0xBE, 0xDE, 0x00}, 2}, {"zo",  {0xBF, 0xDE, 0x00}, 2}, {"da",  {0xC0, 0xDE, 0x00}, 2},
    {"di",  {0xC1, 0xDE, 0x00}, 2}, {"du",  {0xC2, 0xDE, 0x00}, 2}, {"de",  {0xC3, 0xDE, 0x00}, 2},
    {"do",  {0xC4, 0xDE, 0x00}, 2}, {"ba",  {0xCA, 0xDE, 0x00}, 2}, {"bi",  {0xCB, 0xDE, 0x00}, 2},
    {"bu",  {0xCC, 0xDE, 0x00}, 2}, {"be",  {0xCD, 0xDE, 0x00}, 2}, {"bo",  {0xCE, 0xDE, 0x00}, 2},
    {"pa",  {0xCA, 0xDF, 0x00}, 2}, {"pi",  {0xCB, 0xDF, 0x00}, 2}, {"pu",  {0xCC, 0xDF, 0x00}, 2},
    {"pe",  {0xCD, 0xDF, 0x00}, 2}, {"po",  {0xCE, 0xDF, 0x00}, 2}, {"wi",  {0xB2, 0x00, 0x00}, 1},
    {"we",  {0xB4, 0x00, 0x00}, 1}, {"va",  {0xB3, 0xDE, 0xA7}, 3}, {"vi",  {0xB3, 0xDE, 0xA8}, 3},
    {"vu",  {0xB3, 0xDE, 0x00}, 2}, {"ve",  {0xB3, 0xDE, 0xAA}, 3}, {"vo",  {0xB3, 0xDE, 0xAB}, 3},
    {"n",   {0xDD, 0x00, 0x00}, 1},
};

const ImeCandidateEntry kImeCandidateTable[] = {
    {"nihon",   {"\xC6\xCE\xDD", "\xC6\xAF\xCE\xDF\xDD"}, 2},  // ﾆﾎﾝ / ﾆｯﾎﾟﾝ
    {"watashi", {"\xDC\xC0\xBC"}, 1},                          // ﾜﾀｼ
    {"tokyo",   {"\xC4\xB3\xB7\xAE\xB3"}, 1},                  // ﾄｳｷｮｳ
    {"konnichiha", {"\xBA\xDD\xC6\xC1\xCA"}, 1},               // ｺﾝﾆﾁﾊ
    {"ohayou",  {"\xB5\xCA\xAE\xB3"}, 1},                      // ｵﾊﾖｳ
    {"arigatou",{"\xB1\xD8\xB6\xDE\xC4\xB3"}, 1},              // ｱﾘｶﾞﾄｳ
};

ImeUserCandidateEntry g_ime_user_candidates[16];
ImeCandidateEntry g_ime_user_candidate_views[16];
int g_ime_user_candidate_count = 0;
ImeCandidateLearnEntry g_ime_learn_entries[64];
char ToLowerAscii(char c);
const BootFileEntry* FindBootFileByName(const char* name);
void ToLowerAsciiString(const char* src, char* dst, int dst_len);

int CountImeLearningEntries() {
    int count = 0;
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        if (g_ime_learn_entries[i].used) {
            ++count;
        }
    }
    return count;
}

void InitImeUserCandidates() {
    g_ime_user_candidate_count = 0;
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_user_candidates) / sizeof(g_ime_user_candidates[0])); ++i) {
        g_ime_user_candidates[i].used = false;
        g_ime_user_candidates[i].key[0] = '\0';
        g_ime_user_candidates[i].count = 0;
        g_ime_user_candidate_views[i].key = nullptr;
        g_ime_user_candidate_views[i].count = 0;
        for (int j = 0; j < 4; ++j) {
            g_ime_user_candidate_views[i].candidates[j] = nullptr;
        }
        for (int j = 0; j < 4; ++j) {
            g_ime_user_candidates[i].candidates[j][0] = '\0';
        }
    }
}

void InitImeLearning() {
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        g_ime_learn_entries[i].used = false;
        g_ime_learn_entries[i].key[0] = '\0';
        g_ime_learn_entries[i].cand[0] = '\0';
        g_ime_learn_entries[i].score = 0;
    }
}

void ClearImeLearning() {
    InitImeLearning();
}

uint16_t GetImeLearningScore(const char* key, const char* cand) {
    if (key == nullptr || cand == nullptr || key[0] == '\0' || cand[0] == '\0') {
        return 0;
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        if (!g_ime_learn_entries[i].used) {
            continue;
        }
        if (StrEqual(g_ime_learn_entries[i].key, key) && StrEqual(g_ime_learn_entries[i].cand, cand)) {
            return g_ime_learn_entries[i].score;
        }
    }
    return 0;
}

void RecordImeLearning(const char* key, const char* cand) {
    if (key == nullptr || cand == nullptr || key[0] == '\0' || cand[0] == '\0') {
        return;
    }
    int free_slot = -1;
    int min_slot = -1;
    uint16_t min_score = 0xFFFF;
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        if (!g_ime_learn_entries[i].used) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (StrEqual(g_ime_learn_entries[i].key, key) && StrEqual(g_ime_learn_entries[i].cand, cand)) {
            if (g_ime_learn_entries[i].score < 0xFFFF) {
                ++g_ime_learn_entries[i].score;
            }
            return;
        }
        if (g_ime_learn_entries[i].score < min_score) {
            min_score = g_ime_learn_entries[i].score;
            min_slot = i;
        }
    }
    int slot = free_slot;
    if (slot < 0) {
        slot = min_slot;
    }
    if (slot < 0) {
        return;
    }
    g_ime_learn_entries[slot].used = true;
    CopyString(g_ime_learn_entries[slot].key, key, static_cast<int>(sizeof(g_ime_learn_entries[slot].key)));
    CopyString(g_ime_learn_entries[slot].cand, cand, static_cast<int>(sizeof(g_ime_learn_entries[slot].cand)));
    g_ime_learn_entries[slot].score = 1;
}

void UpsertImeLearningScore(const char* key, const char* cand, uint16_t score) {
    if (score == 0 || key == nullptr || cand == nullptr || key[0] == '\0' || cand[0] == '\0') {
        return;
    }
    int free_slot = -1;
    int min_slot = -1;
    uint16_t min_score = 0xFFFF;
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        if (!g_ime_learn_entries[i].used) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (StrEqual(g_ime_learn_entries[i].key, key) && StrEqual(g_ime_learn_entries[i].cand, cand)) {
            g_ime_learn_entries[i].score = score;
            return;
        }
        if (g_ime_learn_entries[i].score < min_score) {
            min_score = g_ime_learn_entries[i].score;
            min_slot = i;
        }
    }
    int slot = (free_slot >= 0) ? free_slot : min_slot;
    if (slot < 0) {
        return;
    }
    g_ime_learn_entries[slot].used = true;
    CopyString(g_ime_learn_entries[slot].key, key, static_cast<int>(sizeof(g_ime_learn_entries[slot].key)));
    CopyString(g_ime_learn_entries[slot].cand, cand, static_cast<int>(sizeof(g_ime_learn_entries[slot].cand)));
    g_ime_learn_entries[slot].score = score;
}

int ParseUnsignedDecimal(const char* s) {
    int value = 0;
    if (s == nullptr || s[0] == '\0') {
        return -1;
    }
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        value = value * 10 + (s[i] - '0');
        if (value > 65535) {
            return -1;
        }
    }
    return value;
}

int ImportImeLearningFromBuffer(const uint8_t* data, int size, bool clear_before) {
    if (data == nullptr || size <= 0) {
        return 0;
    }
    if (clear_before) {
        ClearImeLearning();
    }
    int pos = 0;
    int imported = 0;
    while (pos < size) {
        while (pos < size && (data[pos] == '\r' || data[pos] == '\n')) {
            ++pos;
        }
        if (pos >= size) {
            break;
        }
        if (data[pos] == '#') {
            while (pos < size && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }

        char key[32];
        char cand[32];
        char score_buf[8];
        int key_len = 0;
        int cand_len = 0;
        int score_len = 0;

        while (pos < size && data[pos] != ':' && data[pos] != '\n' && key_len + 1 < static_cast<int>(sizeof(key))) {
            key[key_len++] = static_cast<char>(data[pos++]);
        }
        key[key_len] = '\0';
        if (pos >= size || data[pos] != ':') {
            while (pos < size && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }
        ++pos; // ':'
        while (pos < size && data[pos] != ':' && data[pos] != '\n' && cand_len + 1 < static_cast<int>(sizeof(cand))) {
            cand[cand_len++] = static_cast<char>(data[pos++]);
        }
        cand[cand_len] = '\0';
        if (pos >= size || data[pos] != ':') {
            while (pos < size && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }
        ++pos; // ':'
        while (pos < size && data[pos] != '\n' && data[pos] != '\r' &&
               score_len + 1 < static_cast<int>(sizeof(score_buf))) {
            score_buf[score_len++] = static_cast<char>(data[pos++]);
        }
        score_buf[score_len] = '\0';
        while (pos < size && data[pos] != '\n') {
            ++pos;
        }

        ToLowerAsciiString(key, key, static_cast<int>(sizeof(key)));
        ToLowerAsciiString(cand, cand, static_cast<int>(sizeof(cand)));
        const int parsed = ParseUnsignedDecimal(score_buf);
        if (key[0] == '\0' || cand[0] == '\0' || parsed <= 0) {
            continue;
        }
        UpsertImeLearningScore(key, cand, static_cast<uint16_t>(parsed));
        ++imported;
    }
    return imported;
}

int ExportImeLearningToBuffer(char* out, int out_len) {
    if (out == nullptr || out_len <= 0) {
        return 0;
    }
    int w = 0;
    auto append_char = [&](char c) -> bool {
        if (w + 1 >= out_len) {
            return false;
        }
        out[w++] = c;
        return true;
    };
    auto append_cstr = [&](const char* s) -> bool {
        for (int i = 0; s[i] != '\0'; ++i) {
            if (!append_char(s[i])) {
                return false;
            }
        }
        return true;
    };
    auto append_u16 = [&](uint16_t v) -> bool {
        char rev[6];
        int n = 0;
        do {
            rev[n++] = static_cast<char>('0' + (v % 10));
            v = static_cast<uint16_t>(v / 10);
        } while (v != 0 && n < static_cast<int>(sizeof(rev)));
        for (int i = n - 1; i >= 0; --i) {
            if (!append_char(rev[i])) {
                return false;
            }
        }
        return true;
    };

    append_cstr("# ime learning cache\n");
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_learn_entries) / sizeof(g_ime_learn_entries[0])); ++i) {
        if (!g_ime_learn_entries[i].used) {
            continue;
        }
        if (!append_cstr(g_ime_learn_entries[i].key) ||
            !append_char(':') ||
            !append_cstr(g_ime_learn_entries[i].cand) ||
            !append_char(':') ||
            !append_u16(g_ime_learn_entries[i].score) ||
            !append_char('\n')) {
            break;
        }
    }
    out[w] = '\0';
    return w;
}

int LoadImeLearningFromBootFS() {
    const BootFileEntry* file = FindBootFileByName("ime.learn");
    if (file == nullptr || file->data == nullptr || file->size == 0) {
        return 0;
    }
    const int n = (file->size > 0x7FFFFFFFu) ? 0x7FFFFFFF : static_cast<int>(file->size);
    return ImportImeLearningFromBuffer(file->data, n, false);
}

char ToLowerAscii(char c);
const BootFileEntry* FindBootFileByName(const char* name);

void ToLowerAsciiString(const char* src, char* dst, int dst_len) {
    if (dst_len <= 0) {
        return;
    }
    int w = 0;
    for (int i = 0; src[i] != '\0' && w + 1 < dst_len; ++i) {
        dst[w++] = ToLowerAscii(src[i]);
    }
    dst[w] = '\0';
}

int ParseImeDicToken(const uint8_t* data, int n, int* pos, char* out, int out_len) {
    while (*pos < n && (data[*pos] == ' ' || data[*pos] == '\t')) {
        ++(*pos);
    }
    int w = 0;
    while (*pos < n) {
        const uint8_t ch = data[*pos];
        if (ch == ',' || ch == ':' || ch == '\r' || ch == '\n' || ch == '#') {
            break;
        }
        if (w + 1 < out_len) {
            out[w++] = static_cast<char>(ch);
        }
        ++(*pos);
    }
    while (w > 0 && (out[w - 1] == ' ' || out[w - 1] == '\t')) {
        --w;
    }
    out[w] = '\0';
    return w;
}

void LoadImeDictionaryFromBootFS() {
    InitImeUserCandidates();
    const BootFileEntry* dic = FindBootFileByName("ime.dic");
    if (dic == nullptr) {
        return;
    }
    const uint8_t* data = dic->data;
    const int n = static_cast<int>(dic->size);
    int pos = 0;
    while (pos < n) {
        while (pos < n && (data[pos] == '\r' || data[pos] == '\n')) {
            ++pos;
        }
        if (pos >= n) {
            break;
        }
        if (data[pos] == '#') {
            while (pos < n && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }

        char key[32];
        if (ParseImeDicToken(data, n, &pos, key, sizeof(key)) <= 0) {
            while (pos < n && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }
        while (pos < n && data[pos] != ':' && data[pos] != '\n') {
            ++pos;
        }
        if (pos >= n || data[pos] != ':') {
            while (pos < n && data[pos] != '\n') {
                ++pos;
            }
            continue;
        }
        ++pos;  // skip ':'

        int slot = -1;
        for (int i = 0; i < static_cast<int>(sizeof(g_ime_user_candidates) / sizeof(g_ime_user_candidates[0])); ++i) {
            if (!g_ime_user_candidates[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            break;
        }
        ImeUserCandidateEntry* e = &g_ime_user_candidates[slot];
        e->used = true;
        ToLowerAsciiString(key, e->key, sizeof(e->key));
        e->count = 0;

        while (pos < n && data[pos] != '\n') {
            if (e->count >= 4) {
                while (pos < n && data[pos] != '\n') {
                    ++pos;
                }
                break;
            }
            char cand[32];
            const int clen = ParseImeDicToken(data, n, &pos, cand, sizeof(cand));
            if (clen > 0) {
                ToLowerAsciiString(cand, e->candidates[e->count], sizeof(e->candidates[e->count]));
                ++e->count;
            }
            if (pos < n && data[pos] == ',') {
                ++pos;
                continue;
            }
            while (pos < n && data[pos] != '\n' && data[pos] != ',') {
                ++pos;
            }
            if (pos < n && data[pos] == ',') {
                ++pos;
            }
        }
        if (e->count <= 0) {
            e->used = false;
            e->key[0] = '\0';
        } else {
            g_ime_user_candidate_views[slot].key = e->key;
            g_ime_user_candidate_views[slot].count = e->count;
            for (int j = 0; j < e->count; ++j) {
                g_ime_user_candidate_views[slot].candidates[j] = e->candidates[j];
            }
            ++g_ime_user_candidate_count;
        }
        while (pos < n && data[pos] != '\n') {
            ++pos;
        }
    }
}

const ImeCandidateEntry* FindImeCandidateEntry(const char* key) {
    for (int i = 0; i < static_cast<int>(sizeof(g_ime_user_candidate_views) / sizeof(g_ime_user_candidate_views[0])); ++i) {
        if (g_ime_user_candidate_views[i].key != nullptr && StrEqual(g_ime_user_candidate_views[i].key, key)) {
            return &g_ime_user_candidate_views[i];
        }
    }
    for (int i = 0; i < static_cast<int>(sizeof(kImeCandidateTable) / sizeof(kImeCandidateTable[0])); ++i) {
        if (StrEqual(kImeCandidateTable[i].key, key)) {
            return &kImeCandidateTable[i];
        }
    }
    return nullptr;
}

int RomajiEntryLength(const char* s) {
    int n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

bool RomajiPrefixEquals(const char* buf, int buf_len, const char* roma) {
    const int n = RomajiEntryLength(roma);
    if (buf_len < n) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (buf[i] != roma[i]) {
            return false;
        }
    }
    return true;
}

bool ConvertRomajiHeadToHalfKana(const char* buf,
                                 int buf_len,
                                 bool finalize,
                                 int* consume_len,
                                 uint8_t* out,
                                 int* out_len) {
    *consume_len = 0;
    *out_len = 0;
    if (buf_len <= 0) {
        return false;
    }
    if (buf_len >= 2 &&
        buf[0] == buf[1] &&
        IsRomajiConsonant(buf[0]) &&
        buf[0] != 'n') {
        out[0] = 0xAF;  // small tsu
        *consume_len = 1;
        *out_len = 1;
        return true;
    }
    if (buf[0] == 'n') {
        if (buf_len >= 2) {
            const char next = buf[1];
            if (next == 'n' || (!IsRomajiVowel(next) && next != 'y')) {
                out[0] = 0xDD;
                *consume_len = 1;
                *out_len = 1;
                return true;
            }
        } else if (finalize) {
            out[0] = 0xDD;
            *consume_len = 1;
            *out_len = 1;
            return true;
        }
    }

    int best_n = 0;
    const RomajiKanaEntry* best = nullptr;
    for (int i = 0; i < static_cast<int>(sizeof(kRomajiKanaTable) / sizeof(kRomajiKanaTable[0])); ++i) {
        const int n = RomajiEntryLength(kRomajiKanaTable[i].roma);
        if (n < best_n) {
            continue;
        }
        if (!RomajiPrefixEquals(buf, buf_len, kRomajiKanaTable[i].roma)) {
            continue;
        }
        if (!finalize && buf_len == n && n >= 2) {
            bool has_longer = false;
            for (int j = 0; j < static_cast<int>(sizeof(kRomajiKanaTable) / sizeof(kRomajiKanaTable[0])); ++j) {
                const int m = RomajiEntryLength(kRomajiKanaTable[j].roma);
                if (m <= n || buf_len < m) {
                    continue;
                }
                if (RomajiPrefixEquals(buf, buf_len, kRomajiKanaTable[j].roma)) {
                    has_longer = true;
                    break;
                }
            }
            if (has_longer) {
                continue;
            }
        }
        best = &kRomajiKanaTable[i];
        best_n = n;
    }
    if (best == nullptr) {
        return false;
    }
    for (int i = 0; i < best->len; ++i) {
        out[i] = best->bytes[i];
    }
    *consume_len = best_n;
    *out_len = best->len;
    return true;
}

bool IsAsciiRomajiToken(const char* s) {
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    for (int i = 0; s[i] != '\0'; ++i) {
        const char c = ToLowerAscii(s[i]);
        if (c < 'a' || c > 'z') {
            return false;
        }
    }
    return true;
}

int ConvertRomajiStringToHalfKana(const char* src, char* out, int out_len) {
    if (src == nullptr || out == nullptr || out_len <= 0) {
        return 0;
    }
    char work[64];
    ToLowerAsciiString(src, work, sizeof(work));
    int work_len = StrLength(work);
    int w = 0;
    while (work_len > 0 && w + 1 < out_len) {
        int consume = 0;
        uint8_t kana[3] = {0, 0, 0};
        int kana_len = 0;
        if (!ConvertRomajiHeadToHalfKana(work, work_len, true, &consume, kana, &kana_len)) {
            out[w++] = work[0];
            consume = 1;
        } else {
            for (int i = 0; i < kana_len && w + 1 < out_len; ++i) {
                out[w++] = static_cast<char>(kana[i]);
            }
        }
        for (int i = consume; i <= work_len; ++i) {
            work[i - consume] = work[i];
        }
        work_len -= consume;
    }
    out[w] = '\0';
    return w;
}

void UIntToDecimalString(uint32_t value, char* out, int out_len) {
    if (out == nullptr || out_len <= 0) {
        return;
    }
    char rev[16];
    int n = 0;
    do {
        rev[n++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && n < static_cast<int>(sizeof(rev)));

    int w = 0;
    for (int i = n - 1; i >= 0 && w + 1 < out_len; --i) {
        out[w++] = rev[i];
    }
    out[w] = '\0';
}

void UInt64ToDecimalString(uint64_t value, char* out, int out_len) {
    if (out == nullptr || out_len <= 0) {
        return;
    }
    char rev[32];
    int n = 0;
    do {
        rev[n++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && n < static_cast<int>(sizeof(rev)));

    int w = 0;
    for (int i = n - 1; i >= 0 && w + 1 < out_len; --i) {
        out[w++] = rev[i];
    }
    out[w] = '\0';
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
    if (g_jp_layout) {
        if (g_ime_enabled) {
            console->Print(g_has_halfwidth_kana_font ? "[jp-ime]" : "[jp-ime-ascii]");
        } else {
            console->Print("[jp]");
        }
    } else {
        console->Print(g_ime_enabled ? "[us-ime]" : "[us]");
    }
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
    const int screen_w = static_cast<int>(frame_buffer_config->horizontal_resolution);
    const int screen_h = static_cast<int>(frame_buffer_config->vertical_resolution);
    const int taskbar_h = 34;
    const int term_frame_border = 2;
    const int term_title_h = 24;
    const int info_frame_border = 2;
    const int info_title_h = 24;

    // 3. デスクトップ背景
    Window* bg_window = new Window(screen_w, screen_h);
    bg_window->FillRectangle(0, 0, screen_w, screen_h, {12, 20, 34});
    bg_window->FillRectangle(0, 0, screen_w, screen_h / 3, {18, 30, 48});
    bg_window->FillRectangle(0, screen_h - taskbar_h, screen_w, taskbar_h, {10, 10, 14});
    Layer* bg_layer = layer_manager->NewLayer();
    bg_layer->SetWindow(bg_window).Move(0, 0);
    layer_manager->UpDown(bg_layer, 0);

    // 4. タスクバー
    Window* taskbar_window = new Window(screen_w, taskbar_h);
    taskbar_window->FillRectangle(0, 0, screen_w, taskbar_h, {24, 24, 28});
    taskbar_window->FillRectangle(0, 0, screen_w, 1, {58, 58, 66});
    taskbar_window->DrawString(10, 9, "Native OS", {235, 235, 240});
    taskbar_window->DrawString(screen_w - 108, 9, "Terminal", {190, 190, 205});
    Layer* taskbar_layer = layer_manager->NewLayer();
    taskbar_layer->SetWindow(taskbar_window).Move(0, screen_h - taskbar_h);
    layer_manager->UpDown(taskbar_layer, 1);

    // 5. ターミナルウィンドウ（枠 + コンソール本体）
    int term_content_w = (screen_w * 4) / 5;
    int term_content_h = ((screen_h - taskbar_h) * 3) / 4;
    if (term_content_w < 640) term_content_w = 640;
    if (term_content_h < 360) term_content_h = 360;
    if (term_content_w > screen_w - 24) term_content_w = screen_w - 24;
    if (term_content_h > screen_h - taskbar_h - 24) term_content_h = screen_h - taskbar_h - 24;
    const int term_frame_w = term_content_w + term_frame_border * 2;
    const int term_frame_h = term_content_h + term_title_h + term_frame_border;
    int term_frame_x = (screen_w - term_frame_w) / 2;
    int term_frame_y = (screen_h - taskbar_h - term_frame_h) / 2;
    if (term_frame_x < 8) term_frame_x = 8;
    if (term_frame_y < 8) term_frame_y = 8;

    Window* term_frame_window = new Window(term_frame_w, term_frame_h);
    term_frame_window->FillRectangle(0, 0, term_frame_w, term_frame_h, {74, 76, 86});
    term_frame_window->FillRectangle(term_frame_border, term_frame_border,
                                     term_frame_w - term_frame_border * 2, term_title_h - term_frame_border, {52, 56, 70});
    term_frame_window->FillRectangle(term_frame_border, term_title_h,
                                     term_frame_w - term_frame_border * 2, term_frame_h - term_title_h - term_frame_border, {8, 8, 10});
    term_frame_window->DrawString(10, 5, "Terminal", {238, 238, 242});
    term_frame_window->FillRectangle(term_frame_w - 20, 6, 12, 12, {175, 68, 68});
    Layer* term_frame_layer = layer_manager->NewLayer();
    term_frame_layer->SetWindow(term_frame_window).Move(term_frame_x, term_frame_y);
    layer_manager->UpDown(term_frame_layer, 4);

    Window* term_console_window = new Window(term_content_w, term_content_h);
    term_console_window->FillRectangle(0, 0, term_content_w, term_content_h, {0, 0, 0});
    Layer* term_console_layer = layer_manager->NewLayer();
    term_console_layer->SetWindow(term_console_window).Move(term_frame_x + term_frame_border, term_frame_y + term_title_h);
    layer_manager->UpDown(term_console_layer, 5);

    // 6. Infoウィンドウ（固定情報表示）
    const int info_content_w = 360;
    const int info_content_h = 180;
    const int info_frame_w = info_content_w + info_frame_border * 2;
    const int info_frame_h = info_content_h + info_title_h + info_frame_border;
    int info_frame_x = 32;
    int info_frame_y = 44;
    if (info_frame_x + info_frame_w > screen_w) info_frame_x = screen_w - info_frame_w;
    if (info_frame_y + info_frame_h > screen_h - taskbar_h) info_frame_y = screen_h - taskbar_h - info_frame_h;
    if (info_frame_x < 0) info_frame_x = 0;
    if (info_frame_y < 0) info_frame_y = 0;

    Window* info_frame_window = new Window(info_frame_w, info_frame_h);
    info_frame_window->FillRectangle(0, 0, info_frame_w, info_frame_h, {74, 76, 86});
    info_frame_window->FillRectangle(info_frame_border, info_frame_border,
                                     info_frame_w - info_frame_border * 2, info_title_h - info_frame_border, {46, 50, 62});
    info_frame_window->FillRectangle(info_frame_border, info_title_h,
                                     info_frame_w - info_frame_border * 2, info_frame_h - info_title_h - info_frame_border, {14, 16, 22});
    info_frame_window->DrawString(10, 5, "System", {224, 226, 235});
    info_frame_window->FillRectangle(info_frame_w - 20, 6, 12, 12, {104, 108, 126});
    Layer* info_frame_layer = layer_manager->NewLayer();
    info_frame_layer->SetWindow(info_frame_window).Move(info_frame_x, info_frame_y);
    layer_manager->UpDown(info_frame_layer, 2);

    Window* info_content_window = new Window(info_content_w, info_content_h);
    info_content_window->FillRectangle(0, 0, info_content_w, info_content_h, {14, 16, 22});
    info_content_window->DrawString(12, 12, "Native OS Preview", {220, 224, 232});
    info_content_window->DrawString(12, 34, "Window Manager v1", {180, 188, 204});
    info_content_window->DrawString(12, 56, "- Desktop / Taskbar", {200, 208, 220});
    info_content_window->DrawString(12, 74, "- Draggable windows", {200, 208, 220});
    info_content_window->DrawString(12, 92, "- IME + shell running", {200, 208, 220});
    info_content_window->DrawString(12, 124, "Next:", {224, 226, 235});
    info_content_window->DrawString(12, 142, "Resize / Minimize / Launcher", {180, 188, 204});
    Layer* info_content_layer = layer_manager->NewLayer();
    info_content_layer->SetWindow(info_content_window).Move(info_frame_x + info_frame_border, info_frame_y + info_title_h);
    layer_manager->UpDown(info_content_layer, 3);

    auto DrawFrameTitle = [&](Window* frame, int border, int title_h, int frame_w,
                              const char* title, bool active) {
        PixelColor title_bg = active ? PixelColor{52, 56, 70} : PixelColor{46, 50, 62};
        PixelColor title_fg = active ? PixelColor{238, 238, 242} : PixelColor{206, 208, 220};
        frame->FillRectangle(border, border, frame_w - border * 2, title_h - border, title_bg);
        frame->DrawString(10, 5, title, title_fg);
    };
    DrawFrameTitle(term_frame_window, term_frame_border, term_title_h, term_frame_w, "Terminal", true);
    term_frame_window->FillRectangle(term_frame_w - 20, 6, 12, 12, {175, 68, 68});
    DrawFrameTitle(info_frame_window, info_frame_border, info_title_h, info_frame_w, "System", false);
    info_frame_window->FillRectangle(info_frame_w - 20, 6, 12, 12, {104, 108, 126});

    // 7. コンソールの初期化（描画先をターミナルコンテンツへ）
    console = new(console_buf) Console(term_console_window,
                                        255, 255, 255,
                                        0, 0, 0);
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
                    console->PrintLine("xHCI HID auto-start: failed (PS/2 mouse/keyboard fallback)");
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

    g_has_halfwidth_kana_font = HasHalfwidthKanaFont();
    InitImeLearning();
    LoadImeDictionaryFromBootFS();
    const int loaded_ime_learn = LoadImeLearningFromBootFS();

    console->Print("Waiting for hardware interrupts (Keyboard/Mouse/LAPIC Timer)...\n");
    console->Print("Input mode: layout=");
    console->Print(g_jp_layout ? "jp" : "us");
    console->Print(" ime=");
    console->Print(g_ime_enabled ? "on" : "off");
    console->Print(" ime.font=");
    console->Print(g_has_halfwidth_kana_font ? "halfkana" : "ascii-fallback");
    console->Print(" ime.dic=");
    console->PrintDec(g_ime_user_candidate_count);
    console->Print(" ime.learn=");
    console->PrintDec(CountImeLearningEntries());
    console->Print(" (+");
    console->PrintDec(loaded_ime_learn);
    console->Print(")");
    console->Print("\n");
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
    KeyboardModifiers keyboard_mods;
    InitKeyboardModifiers(&keyboard_mods);
    char command_buffer[128];
    int command_len = 0;
    int cursor_pos = 0;
    command_buffer[0] = '\0';
    char command_history[16][128];
    int history_count = 0;
    int history_nav = -1;   // -1 = browsing off, 0..history_count-1 = selected history row
    char draft_buffer[128];
    draft_buffer[0] = '\0';
    char ime_romaji_buffer[32];
    int ime_romaji_len = 0;
    ime_romaji_buffer[0] = '\0';
    const ImeCandidateEntry* ime_candidate_entry = nullptr;
    bool ime_candidate_active = false;
    int ime_candidate_index = 0;
    int ime_candidate_start = 0;
    int ime_candidate_len = 0;
    ImeCandidateEntry ime_prefix_candidate_view = {};
    const char* ime_prefix_candidate_ptrs[4] = {nullptr, nullptr, nullptr, nullptr};
    char ime_prefix_candidate_key[32];
    char ime_prefix_candidate_texts[4][32];
    char ime_candidate_source_keys[4][32];
    bool e0_prefix = false;
    bool key_down_normal[128];
    bool key_down_extended[128];
    for (int i = 0; i < 128; ++i) {
        key_down_normal[i] = false;
        key_down_extended[i] = false;
    }
    int input_row = console->CursorRow();
    int input_col = console->CursorColumn();
    int rendered_len = 0;
    int selection_anchor = -1;
    int selection_end = -1;
    bool selecting_with_mouse = false;
    int active_window = 0; // 0=terminal, 1=system-info
    int dragging_window = -1;
    int drag_offset_x = 0;
    int drag_offset_y = 0;
    uint64_t next_system_info_tick = 0;
    uint64_t last_drag_redraw_tick = 0;
    uint64_t last_pointer_redraw_tick = 0;
    bool drag_visual_dirty = false;
    int pointer_logical_x = mouse_cursor->X() + 1;
    int pointer_logical_y = mouse_cursor->Y() + 1;
    bool pointer_visual_dirty = false;
    int drag_pending_window = -1;  // 0=terminal, 1=system-info
    int drag_pending_x = 0;
    int drag_pending_y = 0;
    bool drag_pending_move = false;
    auto FlushPendingDrag = [&]() {
        if (!drag_pending_move || drag_pending_window < 0) {
            return;
        }
        auto DrawMovedRects = [&](int old_x, int old_y, int new_x, int new_y, int w, int h) {
            // For large jumps, union area becomes huge and causes stutter.
            // Redraw old and new rectangles separately to keep update cost bounded.
            layer_manager->Draw(old_x, old_y, w, h);
            if (new_x != old_x || new_y != old_y) {
                layer_manager->Draw(new_x, new_y, w, h);
            }
        };
        if (drag_pending_window == 0) {
            const int old_x = term_frame_layer->GetX();
            const int old_y = term_frame_layer->GetY();
            const int new_x = drag_pending_x;
            const int new_y = drag_pending_y;
            if (old_x != new_x || old_y != new_y) {
                term_frame_layer->Move(new_x, new_y);
                term_console_layer->Move(new_x + term_frame_border, new_y + term_title_h);
                DrawMovedRects(old_x, old_y, new_x, new_y, term_frame_w, term_frame_h);
            }
        } else if (drag_pending_window == 1) {
            const int old_x = info_frame_layer->GetX();
            const int old_y = info_frame_layer->GetY();
            const int new_x = drag_pending_x;
            const int new_y = drag_pending_y;
            if (old_x != new_x || old_y != new_y) {
                info_frame_layer->Move(new_x, new_y);
                info_content_layer->Move(new_x + info_frame_border, new_y + info_title_h);
                DrawMovedRects(old_x, old_y, new_x, new_y, info_frame_w, info_frame_h);
            }
        }
        drag_pending_move = false;
        drag_pending_window = -1;
    };
    auto FlushPointerVisual = [&]() {
        if (!pointer_visual_dirty) {
            return;
        }
        mouse_cursor->SetPosition(pointer_logical_x - 1, pointer_logical_y - 1);
        pointer_visual_dirty = false;
    };
    auto RefreshConsole = [&]() {
        layer_manager->Draw(term_console_layer->GetX(),
                            term_console_layer->GetY(),
                            console->PixelWidth(),
                            console->PixelHeight());
    };
    auto RefreshInputLine = [&]() {
        int x = term_console_layer->GetX() + Console::kMarginX + input_col * Console::kCellWidth;
        int y = term_console_layer->GetY() + Console::kMarginY + input_row * Console::kCellHeight;
        int draw_cells = rendered_len + 2;
        if (draw_cells < cursor_pos + 1) {
            draw_cells = cursor_pos + 1;
        }
        const int max_cells = console->Columns() - input_col;
        if (draw_cells > max_cells) {
            draw_cells = max_cells;
        }
        if (draw_cells < 1) {
            draw_cells = 1;
        }
        int w = draw_cells * Console::kCellWidth;
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
    auto RefreshSystemInfo = [&]() {
        info_content_window->FillRectangle(0, 0, info_content_w, info_content_h, {14, 16, 22});
        info_content_window->DrawString(12, 12, "System Monitor", {220, 224, 232});

        char num[40];
        info_content_window->DrawString(12, 34, "tick:", {180, 188, 204});
        UInt64ToDecimalString(CurrentTick(), num, static_cast<int>(sizeof(num)));
        info_content_window->DrawString(88, 34, num, {236, 238, 242});

        uint64_t free_mib = 0;
        if (memory_manager != nullptr) {
            free_mib = (memory_manager->CountFreePages() * kPageSize) / kMiB;
        }
        info_content_window->DrawString(12, 52, "free:", {180, 188, 204});
        UInt64ToDecimalString(free_mib, num, static_cast<int>(sizeof(num)));
        info_content_window->DrawString(88, 52, num, {236, 238, 242});
        info_content_window->DrawString(152, 52, "MiB", {180, 188, 204});

        info_content_window->DrawString(12, 70, "queue:", {180, 188, 204});
        UIntToDecimalString(static_cast<uint32_t>((main_queue != nullptr) ? main_queue->Count() : 0),
                            num, static_cast<int>(sizeof(num)));
        info_content_window->DrawString(88, 70, num, {236, 238, 242});

        info_content_window->DrawString(12, 88, "kbd_drop:", {180, 188, 204});
        UInt64ToDecimalString(g_keyboard_dropped_events, num, static_cast<int>(sizeof(num)));
        info_content_window->DrawString(88, 88, num, {236, 238, 242});

        info_content_window->DrawString(12, 106, "mouse_drop:", {180, 188, 204});
        UInt64ToDecimalString(g_mouse_dropped_events, num, static_cast<int>(sizeof(num)));
        info_content_window->DrawString(88, 106, num, {236, 238, 242});

        info_content_window->DrawString(12, 124, "layout:", {180, 188, 204});
        info_content_window->DrawString(88, 124, g_jp_layout ? "jp" : "us", {236, 238, 242});
        info_content_window->DrawString(128, 124, "ime:", {180, 188, 204});
        info_content_window->DrawString(168, 124, g_ime_enabled ? "on" : "off", {236, 238, 242});

        info_content_window->DrawString(12, 146, "drag/window input optimized", {160, 170, 190});
        layer_manager->Draw(info_content_layer->GetX(), info_content_layer->GetY(), info_content_w, info_content_h);
    };
    RefreshSystemInfo();

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
        auto CountU32Digits = [](uint32_t v) {
            int n = 1;
            while (v >= 10) {
                v /= 10;
                ++n;
            }
            return n;
        };
        int visual_len = command_len;
        if (g_ime_enabled && ime_romaji_len > 0) {
            visual_len += ime_romaji_len + 2; // [romaji]
        }
        if (ime_candidate_active && ime_candidate_entry != nullptr && ime_candidate_entry->count > 0) {
            visual_len += 8; // " [cand "
            visual_len += CountU32Digits(static_cast<uint32_t>(ime_candidate_index + 1));
            visual_len += 1; // '/'
            visual_len += CountU32Digits(static_cast<uint32_t>(ime_candidate_entry->count));
            visual_len += 1; // ']'
        }
        int clear_len = rendered_len;
        if (clear_len < visual_len) {
            clear_len = visual_len;
        }
        clear_len += 2; // trailing safety
        const int max_clear = console->Columns() - input_col - 1;
        if (clear_len > max_clear) {
            clear_len = max_clear;
        }
        if (clear_len < 0) {
            clear_len = 0;
        }
        console->SetCursorPosition(input_row, input_col);
        for (int i = 0; i < clear_len; ++i) {
            console->Print(" ");
        }
        console->SetCursorPosition(input_row, input_col);
        console->Print(command_buffer);
        if (g_ime_enabled && ime_romaji_len > 0) {
            console->Print("[");
            console->Print(ime_romaji_buffer);
            console->Print("]");
        }
        if (ime_candidate_active && ime_candidate_entry != nullptr &&
            ime_candidate_entry->count > 0) {
            char cand_idx[16];
            char cand_total[16];
            UIntToDecimalString(static_cast<uint32_t>(ime_candidate_index + 1), cand_idx, static_cast<int>(sizeof(cand_idx)));
            UIntToDecimalString(static_cast<uint32_t>(ime_candidate_entry->count), cand_total, static_cast<int>(sizeof(cand_total)));
            console->Print(" [cand ");
            console->Print(cand_idx);
            console->Print("/");
            console->Print(cand_total);
            console->Print("]");
        }
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
        rendered_len = visual_len;
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

    auto ClearImeCandidate = [&]() {
        ime_candidate_entry = nullptr;
        ime_candidate_active = false;
        ime_candidate_index = 0;
        ime_candidate_start = 0;
        ime_candidate_len = 0;
        ime_prefix_candidate_view.key = nullptr;
        ime_prefix_candidate_view.count = 0;
        for (int i = 0; i < 4; ++i) {
            ime_prefix_candidate_ptrs[i] = nullptr;
            ime_prefix_candidate_texts[i][0] = '\0';
            ime_prefix_candidate_view.candidates[i] = nullptr;
        }
        ime_prefix_candidate_key[0] = '\0';
        for (int i = 0; i < 4; ++i) {
            ime_candidate_source_keys[i][0] = '\0';
        }
    };

    auto ReplaceInputLine = [&](const char* text) {
        CopyString(command_buffer, text, static_cast<int>(sizeof(command_buffer)));
        command_len = StrLength(command_buffer);
        ime_romaji_len = 0;
        ime_romaji_buffer[0] = '\0';
        ClearImeCandidate();
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

    auto RepaintPromptAndInput = [&]() {
        char snapshot[128];
        CopyString(snapshot, command_buffer, static_cast<int>(sizeof(snapshot)));
        int saved_cursor = cursor_pos;

        console->SetCursorPosition(input_row, 0);
        for (int i = 0; i < console->Columns(); ++i) {
            console->Print(" ");
        }
        console->SetCursorPosition(input_row, 0);
        PrintPrompt();
        input_row = console->CursorRow();
        input_col = console->CursorColumn();
        ReplaceInputLine(snapshot);

        if (saved_cursor < 0) {
            saved_cursor = 0;
        }
        if (saved_cursor > command_len) {
            saved_cursor = command_len;
        }
        if (cursor_pos != saved_cursor) {
            cursor_pos = saved_cursor;
            RenderInputLine();
            RefreshInputLine();
        }
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
        if (g_ime_enabled && ime_romaji_len > 0) {
            --ime_romaji_len;
            ime_romaji_buffer[ime_romaji_len] = '\0';
            RenderInputLine();
            RefreshInputLine();
            return;
        }
        if (ime_candidate_active && ime_candidate_len > 0 &&
            cursor_pos == ime_candidate_start + ime_candidate_len) {
            const int start = ime_candidate_start;
            int len = ime_candidate_len;
            if (start + len > command_len) {
                len = command_len - start;
            }
            for (int i = start; i + len <= command_len; ++i) {
                command_buffer[i] = command_buffer[i + len];
            }
            command_len -= len;
            if (command_len < 0) {
                command_len = 0;
            }
            command_buffer[command_len] = '\0';
            cursor_pos = ime_candidate_start;
            ClearImeCandidate();
            RenderInputLine();
            RefreshInputLine();
            return;
        }
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

    auto DeleteRangeAt = [&](int start, int len) -> bool {
        if (len <= 0 || start < 0 || start > command_len) {
            return false;
        }
        if (start + len > command_len) {
            len = command_len - start;
        }
        for (int i = start; i + len <= command_len; ++i) {
            command_buffer[i] = command_buffer[i + len];
        }
        command_len -= len;
        if (command_len < 0) {
            command_len = 0;
        }
        if (cursor_pos > command_len) {
            cursor_pos = command_len;
        }
        command_buffer[command_len] = '\0';
        return true;
    };

    auto InsertByteAtCursor = [&](uint8_t b) -> bool {
        if (command_len >= MaxInputLen()) {
            return false;
        }
        for (int i = command_len; i > cursor_pos; --i) {
            command_buffer[i] = command_buffer[i - 1];
        }
        command_buffer[cursor_pos] = static_cast<char>(b);
        ++command_len;
        ++cursor_pos;
        command_buffer[command_len] = '\0';
        return true;
    };

    auto InsertCStringAtCursor = [&](const char* text) -> int {
        int inserted = 0;
        for (int i = 0; text[i] != '\0'; ++i) {
            if (!InsertByteAtCursor(static_cast<uint8_t>(text[i]))) {
                break;
            }
            ++inserted;
        }
        return inserted;
    };

    auto ReplaceImeCandidateText = [&]() {
        if (!ime_candidate_active || ime_candidate_entry == nullptr) {
            return;
        }
        const char* cand = ime_candidate_entry->candidates[ime_candidate_index];
        char cand_kana[64];
        const char* insert_text = cand;
        if (IsAsciiRomajiToken(cand)) {
            if (ConvertRomajiStringToHalfKana(cand, cand_kana, static_cast<int>(sizeof(cand_kana))) > 0) {
                insert_text = cand_kana;
            }
        }
        cursor_pos = ime_candidate_start;
        DeleteRangeAt(ime_candidate_start, ime_candidate_len);
        cursor_pos = ime_candidate_start;
        ime_candidate_len = InsertCStringAtCursor(insert_text);
        cursor_pos = ime_candidate_start + ime_candidate_len;
        RenderInputLine();
        RefreshInputLine();
    };
    auto CommitImeCandidateLearning = [&]() {
        if (!ime_candidate_active || ime_candidate_entry == nullptr ||
            ime_candidate_index < 0 || ime_candidate_index >= ime_candidate_entry->count) {
            return;
        }
        const char* cand = ime_candidate_entry->candidates[ime_candidate_index];
        if (cand == nullptr || cand[0] == '\0') {
            return;
        }
        const char* key = nullptr;
        if (ime_candidate_index < 4 && ime_candidate_source_keys[ime_candidate_index][0] != '\0') {
            key = ime_candidate_source_keys[ime_candidate_index];
        } else {
            key = ime_candidate_entry->key;
        }
        RecordImeLearning(key, cand);
    };
    auto FindBestImeCandidateIndex = [&](const ImeCandidateEntry* entry) -> int {
        if (entry == nullptr || entry->count <= 0) {
            return 0;
        }
        int best_idx = 0;
        uint16_t best_score = 0;
        for (int i = 0; i < entry->count && i < 4; ++i) {
            const char* cand = entry->candidates[i];
            const char* key = (ime_candidate_source_keys[i][0] != '\0') ? ime_candidate_source_keys[i] : entry->key;
            const uint16_t score = GetImeLearningScore(key, cand);
            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }
        return best_idx;
    };
    auto CycleImeCandidate = [&](int delta) -> bool {
        if (!ime_candidate_active || ime_candidate_entry == nullptr || ime_candidate_entry->count <= 0) {
            return false;
        }
        int next = ime_candidate_index + delta;
        const int count = ime_candidate_entry->count;
        while (next < 0) {
            next += count;
        }
        while (next >= count) {
            next -= count;
        }
        ime_candidate_index = next;
        ReplaceImeCandidateText();
        return true;
    };
    auto TryBuildPrefixCandidateEntry = [&](const char* prefix) -> const ImeCandidateEntry* {
        if (prefix == nullptr || prefix[0] == '\0') {
            return nullptr;
        }
        const int prefix_len = StrLength(prefix);
        CopyString(ime_prefix_candidate_key, prefix, static_cast<int>(sizeof(ime_prefix_candidate_key)));
        ime_prefix_candidate_view.key = ime_prefix_candidate_key;
        ime_prefix_candidate_view.count = 0;
        for (int i = 0; i < 4; ++i) {
            ime_prefix_candidate_ptrs[i] = nullptr;
            ime_prefix_candidate_texts[i][0] = '\0';
            ime_prefix_candidate_view.candidates[i] = nullptr;
        }
        auto append_unique_candidates = [&](const ImeCandidateEntry* src) {
            if (src == nullptr || src->count <= 0) {
                return;
            }
            for (int i = 0; i < src->count; ++i) {
                const char* cand = src->candidates[i];
                if (cand == nullptr || cand[0] == '\0') {
                    continue;
                }
                bool exists = false;
                for (int j = 0; j < ime_prefix_candidate_view.count; ++j) {
                    if (StrEqual(ime_prefix_candidate_ptrs[j], cand)) {
                        exists = true;
                        break;
                    }
                }
                if (exists) {
                    continue;
                }
                if (ime_prefix_candidate_view.count >= 4) {
                    return;
                }
                const int dst_index = ime_prefix_candidate_view.count;
                CopyString(ime_prefix_candidate_texts[dst_index], cand, static_cast<int>(sizeof(ime_prefix_candidate_texts[dst_index])));
                ime_prefix_candidate_ptrs[dst_index] = ime_prefix_candidate_texts[dst_index];
                ime_prefix_candidate_view.candidates[dst_index] = ime_prefix_candidate_ptrs[dst_index];
                CopyString(ime_candidate_source_keys[dst_index], src->key, static_cast<int>(sizeof(ime_candidate_source_keys[dst_index])));
                ++ime_prefix_candidate_view.count;
            }
        };
        int max_key_len = prefix_len;
        for (int i = 0; i < static_cast<int>(sizeof(g_ime_user_candidate_views) / sizeof(g_ime_user_candidate_views[0])); ++i) {
            const ImeCandidateEntry* e = &g_ime_user_candidate_views[i];
            if (e->key == nullptr || !StrStartsWith(e->key, prefix)) {
                continue;
            }
            const int key_len = StrLength(e->key);
            if (key_len > max_key_len) {
                max_key_len = key_len;
            }
        }
        for (int i = 0; i < static_cast<int>(sizeof(kImeCandidateTable) / sizeof(kImeCandidateTable[0])); ++i) {
            const ImeCandidateEntry* e = &kImeCandidateTable[i];
            if (!StrStartsWith(e->key, prefix)) {
                continue;
            }
            const int key_len = StrLength(e->key);
            if (key_len > max_key_len) {
                max_key_len = key_len;
            }
        }
        for (int target_len = max_key_len; target_len >= prefix_len; --target_len) {
            for (int i = 0; i < static_cast<int>(sizeof(g_ime_user_candidate_views) / sizeof(g_ime_user_candidate_views[0])); ++i) {
                const ImeCandidateEntry* e = &g_ime_user_candidate_views[i];
                if (e->key == nullptr || !StrStartsWith(e->key, prefix)) {
                    continue;
                }
                if (StrLength(e->key) != target_len) {
                    continue;
                }
                append_unique_candidates(e);
                if (ime_prefix_candidate_view.count >= 4) {
                    return &ime_prefix_candidate_view;
                }
            }
            for (int i = 0; i < static_cast<int>(sizeof(kImeCandidateTable) / sizeof(kImeCandidateTable[0])); ++i) {
                const ImeCandidateEntry* e = &kImeCandidateTable[i];
                if (!StrStartsWith(e->key, prefix)) {
                    continue;
                }
                if (StrLength(e->key) != target_len) {
                    continue;
                }
                append_unique_candidates(e);
                if (ime_prefix_candidate_view.count >= 4) {
                    return &ime_prefix_candidate_view;
                }
            }
        }
        if (ime_prefix_candidate_view.count <= 0) {
            return nullptr;
        }
        for (int i = 0; i + 1 < ime_prefix_candidate_view.count; ++i) {
            for (int j = i + 1; j < ime_prefix_candidate_view.count; ++j) {
                const uint16_t score_i = GetImeLearningScore(ime_candidate_source_keys[i], ime_prefix_candidate_view.candidates[i]);
                const uint16_t score_j = GetImeLearningScore(ime_candidate_source_keys[j], ime_prefix_candidate_view.candidates[j]);
                if (score_j <= score_i) {
                    continue;
                }
                const char* tmp_ptr = ime_prefix_candidate_view.candidates[i];
                ime_prefix_candidate_view.candidates[i] = ime_prefix_candidate_view.candidates[j];
                ime_prefix_candidate_view.candidates[j] = tmp_ptr;
                char tmp_key[32];
                CopyString(tmp_key, ime_candidate_source_keys[i], static_cast<int>(sizeof(tmp_key)));
                CopyString(ime_candidate_source_keys[i], ime_candidate_source_keys[j], static_cast<int>(sizeof(ime_candidate_source_keys[i])));
                CopyString(ime_candidate_source_keys[j], tmp_key, static_cast<int>(sizeof(ime_candidate_source_keys[j])));
            }
        }
        return &ime_prefix_candidate_view;
    };

    auto FlushImeRomaji = [&](bool finalize) -> bool {
        if (ime_romaji_len <= 0) {
            return false;
        }
        ClearImeCandidate();
        const int before_len = ime_romaji_len;
        bool inserted = false;
        if (HasSelection()) {
            DeleteSelection();
        }
        while (ime_romaji_len > 0) {
            int consume = 0;
            uint8_t kana_bytes[3] = {0, 0, 0};
            int kana_len = 0;
            if (!ConvertRomajiHeadToHalfKana(ime_romaji_buffer,
                                             ime_romaji_len,
                                             finalize,
                                             &consume,
                                             kana_bytes,
                                             &kana_len)) {
                break;
            }
            for (int i = 0; i < kana_len; ++i) {
                if (!InsertByteAtCursor(kana_bytes[i])) {
                    break;
                }
                inserted = true;
            }
            for (int i = consume; i <= ime_romaji_len; ++i) {
                ime_romaji_buffer[i - consume] = ime_romaji_buffer[i];
            }
            ime_romaji_len -= consume;
            if (ime_romaji_len < 0) {
                ime_romaji_len = 0;
                ime_romaji_buffer[0] = '\0';
                break;
            }
        }
        if (finalize && ime_romaji_len > 0) {
            for (int i = 0; i < ime_romaji_len; ++i) {
                if (!InsertByteAtCursor(static_cast<uint8_t>(ime_romaji_buffer[i]))) {
                    break;
                }
                inserted = true;
            }
            ime_romaji_len = 0;
            ime_romaji_buffer[0] = '\0';
        }
        if (inserted) {
            RenderInputLine();
            RefreshInputLine();
        } else if (ime_romaji_len != before_len) {
            RenderInputLine();
            RefreshInputLine();
        }
        return inserted;
    };

    auto HandleMouseMessage = [&](const Message& msg) {
        const uint8_t prev_buttons = g_mouse_buttons_current;
        const uint8_t now_buttons = msg.buttons;
        const uint8_t pressed = static_cast<uint8_t>((~prev_buttons) & now_buttons);
        if ((pressed & 0x01) != 0) { ++g_mouse_left_press_count; }
        if ((pressed & 0x02) != 0) { ++g_mouse_right_press_count; }
        if ((pressed & 0x04) != 0) { ++g_mouse_middle_press_count; }
        g_mouse_buttons_current = now_buttons;

        int pointer_x = pointer_logical_x;
        int pointer_y = pointer_logical_y;
        if (msg.pointer_mode == Message::PointerMode::kAbsolute) {
            g_last_absolute_mouse_tick = CurrentTick();
            pointer_x = msg.x;
            pointer_y = msg.y;
        } else {
            if (g_xhci_hid_auto_enabled &&
                (CurrentTick() - g_last_absolute_mouse_tick) < 1000) {
                return;  // USB absolute pointer is active; ignore noisy PS/2 relative moves.
            }
            pointer_x += msg.dx;
            pointer_y += msg.dy;
        }
        if (pointer_x < 0) pointer_x = 0;
        if (pointer_y < 0) pointer_y = 0;
        if (pointer_x >= screen_w) pointer_x = screen_w - 1;
        if (pointer_y >= screen_h) pointer_y = screen_h - 1;
        if (pointer_x != pointer_logical_x || pointer_y != pointer_logical_y) {
            pointer_logical_x = pointer_x;
            pointer_logical_y = pointer_y;
            pointer_visual_dirty = true;
        }
        {
            const int frame_x = term_frame_layer->GetX();
            const int frame_y = term_frame_layer->GetY();
            const int local_frame_x = pointer_x - frame_x;
            const int local_frame_y = pointer_y - frame_y;
            const bool on_term_title =
                local_frame_x >= 0 && local_frame_x < term_frame_w &&
                local_frame_y >= 0 && local_frame_y < term_title_h;
            const bool in_term_frame =
                local_frame_x >= 0 && local_frame_x < term_frame_w &&
                local_frame_y >= 0 && local_frame_y < term_frame_h;
            const int info_x = info_frame_layer->GetX();
            const int info_y = info_frame_layer->GetY();
            const int local_info_x = pointer_x - info_x;
            const int local_info_y = pointer_y - info_y;
            const bool on_info_title =
                local_info_x >= 0 && local_info_x < info_frame_w &&
                local_info_y >= 0 && local_info_y < info_title_h;
            const bool in_info_frame =
                local_info_x >= 0 && local_info_x < info_frame_w &&
                local_info_y >= 0 && local_info_y < info_frame_h;
            auto ApplyWindowFocus = [&](int which) {
                if (which == active_window) {
                    return;
                }
                const int term_frame_x0 = term_frame_layer->GetX();
                const int term_frame_y0 = term_frame_layer->GetY();
                const int info_frame_x0 = info_frame_layer->GetX();
                const int info_frame_y0 = info_frame_layer->GetY();
                active_window = which;
                if (which == 0) {
                    layer_manager->UpDown(info_frame_layer, 2);
                    layer_manager->UpDown(info_content_layer, 3);
                    layer_manager->UpDown(term_frame_layer, 4);
                    layer_manager->UpDown(term_console_layer, 5);
                    DrawFrameTitle(term_frame_window, term_frame_border, term_title_h, term_frame_w, "Terminal", true);
                    term_frame_window->FillRectangle(term_frame_w - 20, 6, 12, 12, {175, 68, 68});
                    DrawFrameTitle(info_frame_window, info_frame_border, info_title_h, info_frame_w, "System", false);
                    info_frame_window->FillRectangle(info_frame_w - 20, 6, 12, 12, {104, 108, 126});
                } else {
                    layer_manager->UpDown(term_frame_layer, 2);
                    layer_manager->UpDown(term_console_layer, 3);
                    layer_manager->UpDown(info_frame_layer, 4);
                    layer_manager->UpDown(info_content_layer, 5);
                    DrawFrameTitle(term_frame_window, term_frame_border, term_title_h, term_frame_w, "Terminal", false);
                    term_frame_window->FillRectangle(term_frame_w - 20, 6, 12, 12, {130, 74, 74});
                    DrawFrameTitle(info_frame_window, info_frame_border, info_title_h, info_frame_w, "System", true);
                    info_frame_window->FillRectangle(info_frame_w - 20, 6, 12, 12, {104, 108, 126});
                }
                layer_manager->Draw(term_frame_x0, term_frame_y0, term_frame_w, term_frame_h);
                layer_manager->Draw(info_frame_x0, info_frame_y0, info_frame_w, info_frame_h);
            };
            if ((pressed & 0x01) != 0) {
                int hit_window = -1;
                if (active_window == 0) {
                    if (in_term_frame) {
                        hit_window = 0;
                    } else if (in_info_frame) {
                        hit_window = 1;
                    }
                } else {
                    if (in_info_frame) {
                        hit_window = 1;
                    } else if (in_term_frame) {
                        hit_window = 0;
                    }
                }
                if (hit_window >= 0) {
                    ApplyWindowFocus(hit_window);
                }
                if (hit_window == 0 && on_term_title) {
                    dragging_window = 0;
                    drag_offset_x = local_frame_x;
                    drag_offset_y = local_frame_y;
                    ClearSelection();
                } else if (hit_window == 1 && on_info_title) {
                    dragging_window = 1;
                    drag_offset_x = local_info_x;
                    drag_offset_y = local_info_y;
                    ClearSelection();
                }
            }
            if (((prev_buttons & 0x01) != 0) && ((now_buttons & 0x01) == 0)) {
                selecting_with_mouse = false;
                if (drag_visual_dirty) {
                    FlushPendingDrag();
                    drag_visual_dirty = false;
                }
                dragging_window = -1;
            }
            if ((now_buttons & 0x01) != 0 && dragging_window >= 0) {
                if (dragging_window == 0) {
                    int new_frame_x = pointer_x - drag_offset_x;
                    int new_frame_y = pointer_y - drag_offset_y;
                    if (new_frame_x < 0) new_frame_x = 0;
                    if (new_frame_y < 0) new_frame_y = 0;
                    if (new_frame_x > screen_w - term_frame_w) new_frame_x = screen_w - term_frame_w;
                    if (new_frame_y > screen_h - taskbar_h - term_frame_h) new_frame_y = screen_h - taskbar_h - term_frame_h;
                    if (new_frame_x != term_frame_layer->GetX() || new_frame_y != term_frame_layer->GetY() ||
                        (drag_pending_move && drag_pending_window == 0 &&
                         (drag_pending_x != new_frame_x || drag_pending_y != new_frame_y))) {
                        drag_pending_window = 0;
                        drag_pending_x = new_frame_x;
                        drag_pending_y = new_frame_y;
                        drag_pending_move = true;
                        drag_visual_dirty = true;
                    }
                } else {
                    int new_info_x = pointer_x - drag_offset_x;
                    int new_info_y = pointer_y - drag_offset_y;
                    if (new_info_x < 0) new_info_x = 0;
                    if (new_info_y < 0) new_info_y = 0;
                    if (new_info_x > screen_w - info_frame_w) new_info_x = screen_w - info_frame_w;
                    if (new_info_y > screen_h - taskbar_h - info_frame_h) new_info_y = screen_h - taskbar_h - info_frame_h;
                    if (new_info_x != info_frame_layer->GetX() || new_info_y != info_frame_layer->GetY() ||
                        (drag_pending_move && drag_pending_window == 1 &&
                         (drag_pending_x != new_info_x || drag_pending_y != new_info_y))) {
                        drag_pending_window = 1;
                        drag_pending_x = new_info_x;
                        drag_pending_y = new_info_y;
                        drag_pending_move = true;
                        drag_visual_dirty = true;
                    }
                }
                return;
            }

            const int console_x = term_console_layer->GetX();
            const int console_y = term_console_layer->GetY();
            const bool in_terminal_console =
                pointer_x >= console_x && pointer_x < console_x + term_content_w &&
                pointer_y >= console_y && pointer_y < console_y + term_content_h;
            if (!in_terminal_console) {
                if ((pressed & 0x01) != 0) {
                    ClearSelection();
                }
                return;
            }
            const int click_col = (pointer_x - console_x - Console::kMarginX) / Console::kCellWidth;
            const int click_row = (pointer_y - console_y - Console::kMarginY) / Console::kCellHeight;
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
        }
        if (msg.wheel > 0) {
            console->ScrollUp(msg.wheel * 3);
            RefreshConsole();
        } else if (msg.wheel < 0) {
            console->ScrollDown((-msg.wheel) * 3);
            RefreshConsole();
        }
    };

    auto HandleExtendedKey = [&](uint8_t key) -> bool {
        if (ime_candidate_active && ime_candidate_entry != nullptr) {
            if (key == 0x48) { // Arrow Up
                EnsureLiveConsole();
                return CycleImeCandidate(-1);
            }
            if (key == 0x50) { // Arrow Down
                EnsureLiveConsole();
                return CycleImeCandidate(1);
            }
        }
        if (g_ime_enabled && ime_romaji_len > 0) {
            FlushImeRomaji(true);
        }
        if (ime_candidate_active) {
            ClearImeCandidate();
        }
        if (key == 0x49) { // Page Up
            console->ScrollUp(3);
            RefreshConsole();
            return true;
        } else if (key == 0x51) { // Page Down
            console->ScrollDown(3);
            RefreshConsole();
            return true;
        } else if (key == 0x53) { // Delete
            EnsureLiveConsole();
            DeleteAtCursor();
            return true;
        } else if (key == 0x4B) { // Arrow Left
            EnsureLiveConsole();
            if (cursor_pos > 0) {
                ClearSelection();
                --cursor_pos;
                RenderInputLine();
                RefreshInputLine();
            }
            return true;
        } else if (key == 0x4D) { // Arrow Right
            EnsureLiveConsole();
            if (cursor_pos < command_len) {
                ClearSelection();
                ++cursor_pos;
                RenderInputLine();
                RefreshInputLine();
            }
            return true;
        } else if (key == 0x47) { // Home
            EnsureLiveConsole();
            ClearSelection();
            cursor_pos = 0;
            RenderInputLine();
            RefreshInputLine();
            return true;
        } else if (key == 0x4F) { // End
            EnsureLiveConsole();
            ClearSelection();
            cursor_pos = command_len;
            RenderInputLine();
            RefreshInputLine();
            return true;
        } else if (key == 0x48) { // Arrow Up
            EnsureLiveConsole();
            BrowseHistoryUp();
            return true;
        } else if (key == 0x50) { // Arrow Down
            EnsureLiveConsole();
            BrowseHistoryDown();
            return true;
        }
        return false;
    };

    auto HandleRegularKeyShortcut = [&](uint8_t key) {
        if (ime_candidate_active && key != 0x39 && key != 0x01) {
            CommitImeCandidateLearning();
            ClearImeCandidate();
        }
        if (key == 0x01) { // Esc
            if (ime_candidate_active && ime_candidate_entry != nullptr) {
                const char* src = ime_candidate_entry->key;
                int src_len = StrLength(src);
                cursor_pos = ime_candidate_start;
                DeleteRangeAt(ime_candidate_start, ime_candidate_len);
                cursor_pos = ime_candidate_start;
                ime_romaji_len = 0;
                for (int i = 0; i < src_len && i + 1 < static_cast<int>(sizeof(ime_romaji_buffer)); ++i) {
                    ime_romaji_buffer[i] = src[i];
                    ime_romaji_len = i + 1;
                }
                ime_romaji_buffer[ime_romaji_len] = '\0';
                ClearImeCandidate();
                RenderInputLine();
                RefreshInputLine();
                return true;
            }
            if (g_ime_enabled && ime_romaji_len > 0) {
                ime_romaji_len = 0;
                ime_romaji_buffer[0] = '\0';
                RenderInputLine();
                RefreshInputLine();
                return true;
            }
        }
        if (IsCtrlPressed(keyboard_mods)) {
            if (key == 0x39) { // Ctrl + Space => IME toggle fallback
                FlushImeRomaji(true);
                g_ime_enabled = !g_ime_enabled;
                if (g_ime_enabled) {
                    g_jp_layout = true;
                }
                RepaintPromptAndInput();
                return true;
            }
            if (key == 0x1E) { // Ctrl + A
                if (g_ime_enabled && ime_romaji_len > 0) {
                    FlushImeRomaji(true);
                }
                EnsureLiveConsole();
                ClearSelection();
                cursor_pos = 0;
                RenderInputLine();
                RefreshInputLine();
                return true;
            }
            if (key == 0x12) { // Ctrl + E
                if (g_ime_enabled && ime_romaji_len > 0) {
                    FlushImeRomaji(true);
                }
                EnsureLiveConsole();
                ClearSelection();
                cursor_pos = command_len;
                RenderInputLine();
                RefreshInputLine();
                return true;
            }
            if (key == 0x26) { // Ctrl + L
                if (g_ime_enabled && ime_romaji_len > 0) {
                    FlushImeRomaji(true);
                }
                console->Clear();
                PrintPrompt();
                input_row = console->CursorRow();
                input_col = console->CursorColumn();
                rendered_len = 0;
                command_len = 0;
                cursor_pos = 0;
                command_buffer[0] = '\0';
                ime_romaji_len = 0;
                ime_romaji_buffer[0] = '\0';
                ClearImeCandidate();
                ClearSelection();
                history_nav = -1;
                draft_buffer[0] = '\0';
                RenderInputLine();
                RefreshInputLine();
                return true;
            }
        }
        if (!keyboard_mods.num_lock && key == 0x47) { // Home (non-E0 fallback)
            if (g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            ClearSelection();
            cursor_pos = 0;
            RenderInputLine();
            RefreshInputLine();
            return true;
        }
        if (!keyboard_mods.num_lock && key == 0x48) { // Arrow Up (non-E0 fallback)
            if (g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            if (CycleImeCandidate(-1)) {
                return true;
            }
            BrowseHistoryUp();
            return true;
        }
        if (!keyboard_mods.num_lock && key == 0x4F) { // End (non-E0 fallback)
            if (g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            ClearSelection();
            cursor_pos = command_len;
            RenderInputLine();
            RefreshInputLine();
            return true;
        }
        if (!keyboard_mods.num_lock && key == 0x50) { // Arrow Down (non-E0 fallback)
            if (g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            if (CycleImeCandidate(1)) {
                return true;
            }
            BrowseHistoryDown();
            return true;
        }
        if (key == 0x0E || ((!keyboard_mods.num_lock) && (key == 0x53 || key == 0x71))) { // Backspace/Delete
            if (key != 0x0E && g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            if (key == 0x0E) {
                BackspaceAtCursor();
            } else {
                DeleteAtCursor();
            }
            return true;
        }
        if (key == 0x0F) { // Tab
            if (g_ime_enabled && ime_romaji_len > 0) {
                FlushImeRomaji(true);
            }
            EnsureLiveConsole();
            HandleTabCompletion();
            return true;
        }
        if (key == 0x29) { // Hankaku/Zenkaku (JP)
            FlushImeRomaji(true);
            g_ime_enabled = !g_ime_enabled;
            g_jp_layout = true;
            RepaintPromptAndInput();
            return true;
        }
        if (key == 0x70) { // Kana
            FlushImeRomaji(true);
            g_ime_enabled = !g_ime_enabled;
            if (g_ime_enabled) {
                g_jp_layout = true;
            }
            RepaintPromptAndInput();
            return true;
        }
        if (key == 0x79 || key == 0x7B) { // Henkan / Muhenkan
            FlushImeRomaji(true);
            g_ime_enabled = (key == 0x79);
            if (g_ime_enabled) {
                g_jp_layout = true;
            }
            RepaintPromptAndInput();
            return true;
        }
        return false;
    };

    while (1) {
        // 処理すべきイベントがあるか、割り込みを禁止(cli)した上で安全にチェックする（競合対策）
        __asm__ volatile("cli");
        if (main_queue->Count() == 0) {
            __asm__ volatile("sti");
            if (HandleXHCIAutoPollOnIdle()) {
                continue;
            }
            // キューが空ならばCPUを休止(hlt)させる
            __asm__ volatile("hlt");
            continue;
        }

        // キューにデータが入っていたら、メッセージを1つ取り出す
        Message msg;
        main_queue->Pop(msg);
        if (msg.type == Message::Type::kInterruptMouse && msg.wheel == 0) {
            Message next;
            if (msg.pointer_mode == Message::PointerMode::kRelative) {
                int merged = 0;
                int total_dx = msg.dx;
                int total_dy = msg.dy;
                int max_merge = (msg.buttons == 0) ? 8 : 32;
                if (main_queue != nullptr) {
                    const int backlog = main_queue->Count();
                    if (backlog > 64) {
                        max_merge = (msg.buttons == 0) ? 64 : 96;
                    } else if (backlog > 24) {
                        max_merge = (msg.buttons == 0) ? 32 : 64;
                    }
                }
                while (merged < max_merge &&
                       main_queue->Peek(next) &&
                       next.type == Message::Type::kInterruptMouse &&
                       next.pointer_mode == Message::PointerMode::kRelative &&
                       next.wheel == 0) {
                    main_queue->Pop(next);
                    ++merged;
                    // Keep interaction state fresh first: latest button state wins.
                    msg.buttons = next.buttons;
                    // Preserve full motion amount even when events are coalesced.
                    total_dx += next.dx;
                    total_dy += next.dy;
                }
                msg.dx = total_dx;
                msg.dy = total_dy;
            } else {
                int merged = 0;
                int last_x = msg.x;
                int last_y = msg.y;
                while (merged < 128 &&
                       main_queue->Peek(next) &&
                       next.type == Message::Type::kInterruptMouse &&
                       next.pointer_mode == Message::PointerMode::kAbsolute &&
                       next.wheel == 0) {
                    main_queue->Pop(next);
                    ++merged;
                    msg.buttons = next.buttons;
                    last_x = next.x;
                    last_y = next.y;
                }
                msg.x = last_x;
                msg.y = last_y;
            }
        }
        
        // 取り出し終わったら割り込みを再開する
        __asm__ volatile("sti");

        // 取り出したメッセージの種類ごとに重い処理（状態の更新）を行う
        switch (msg.type) {
            case Message::Type::kInterruptMouse:
                HandleMouseMessage(msg);
                break;
            case Message::Type::kInterruptKeyboard: {
                ++g_keyboard_irq_count;
                g_keyboard_last_raw = msg.keycode;
                KeyEvent key_event{};
                if (!DecodePS2Set1KeyEvent(msg.keycode, &e0_prefix, &keyboard_mods, &key_event)) {
                    break;
                }
                g_keyboard_last_key = key_event.keycode;
                g_keyboard_last_extended = key_event.extended;
                g_keyboard_last_released = key_event.released;
                if (key_event.kind == KeyEventKind::kModifier) {
                    break;
                }
                if (key_event.released) {
                    if (key_event.keycode < 128) {
                        if (key_event.extended) {
                            key_down_extended[key_event.keycode] = false;
                        } else {
                            key_down_normal[key_event.keycode] = false;
                        }
                    }
                    break;
                }
                const uint8_t key = key_event.keycode;
                if (key_event.extended) {
                    if (HandleExtendedKey(key)) {
                        break;
                    }
                    // Extended keypad Enter and keypad Slash should behave as text input keys.
                    if (key != 0x1C && key != 0x35) {
                        break;
                    }
                }
                if (key < 128 && !g_key_repeat_enabled) {
                    const bool already_down = key_event.extended ? key_down_extended[key] : key_down_normal[key];
                    if (already_down) {
                        break;
                    }
                }
                if (key < 128) {
                    if (key_event.extended) {
                        key_down_extended[key] = true;
                    } else {
                        key_down_normal[key] = true;
                    }
                }
                if (HandleRegularKeyShortcut(key)) {
                    break;
                }

                char ch = KeycodeToAsciiByLayout(key,
                                                 key_event.shift,
                                                 key_event.caps_lock,
                                                 key_event.num_lock,
                                                 g_jp_layout);
                if (ch != 0) {
                    EnsureLiveConsole();
                    bool full_refresh = false;
                    if (g_ime_enabled && g_jp_layout && g_has_halfwidth_kana_font) {
                        if (ch == ' ' && ime_candidate_active && ime_candidate_entry != nullptr) {
                            ime_candidate_index = (ime_candidate_index + 1) % ime_candidate_entry->count;
                            ReplaceImeCandidateText();
                            break;
                        }
                        if (ime_candidate_active && ch != ' ') {
                            CommitImeCandidateLearning();
                            ClearImeCandidate();
                        }
                        char lower = ToLowerAscii(ch);
                        const bool is_alpha = (lower >= 'a' && lower <= 'z');
                        if (is_alpha) {
                            if (ime_romaji_len + 1 < static_cast<int>(sizeof(ime_romaji_buffer))) {
                                ime_romaji_buffer[ime_romaji_len++] = lower;
                                ime_romaji_buffer[ime_romaji_len] = '\0';
                                if (!FlushImeRomaji(false)) {
                                    RenderInputLine();
                                    RefreshInputLine();
                                }
                            }
                            break;
                        }
                        if (ch == ' ' && ime_romaji_len > 0) {
                            char keybuf[32];
                            for (int i = 0; i < ime_romaji_len && i + 1 < static_cast<int>(sizeof(keybuf)); ++i) {
                                keybuf[i] = ToLowerAscii(ime_romaji_buffer[i]);
                                keybuf[i + 1] = '\0';
                            }
                            const ImeCandidateEntry* entry = FindImeCandidateEntry(keybuf);
                            if (entry == nullptr) {
                                entry = TryBuildPrefixCandidateEntry(keybuf);
                            }
                            if (entry != nullptr && entry->count > 0) {
                                if (HasSelection()) {
                                    DeleteSelection();
                                }
                                ime_candidate_entry = entry;
                                ime_candidate_active = true;
                                for (int i = 0; i < 4; ++i) {
                                    ime_candidate_source_keys[i][0] = '\0';
                                }
                                for (int i = 0; i < entry->count && i < 4; ++i) {
                                    CopyString(ime_candidate_source_keys[i], entry->key, static_cast<int>(sizeof(ime_candidate_source_keys[i])));
                                }
                                ime_candidate_index = FindBestImeCandidateIndex(entry);
                                ime_candidate_start = cursor_pos;
                                ime_candidate_len = 0;
                                ime_romaji_len = 0;
                                ime_romaji_buffer[0] = '\0';
                                ReplaceImeCandidateText();
                                break;
                            }
                        }
                        // Finalize pending romaji before non-alpha key (space/punct/enter).
                        FlushImeRomaji(true);
                    }
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
                        ime_romaji_len = 0;
                        ime_romaji_buffer[0] = '\0';
                        ClearImeCandidate();
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
                        if (InsertByteAtCursor(static_cast<uint8_t>(ch))) {
                            RenderInputLine();
                        }
                    }
                    if (full_refresh) {
                        RefreshConsole();
                    } else {
                        RefreshInputLine();
                    }
                }
                break;
            }
            default:
                break;
        }

        const uint64_t now_tick = CurrentTick();
        if (drag_visual_dirty && now_tick != last_drag_redraw_tick) {
            FlushPendingDrag();
            drag_visual_dirty = false;
            last_drag_redraw_tick = now_tick;
        }
        if (pointer_visual_dirty && now_tick != last_pointer_redraw_tick) {
            // Queue-aware flush: keep smoothness while avoiding overdraw under heavy input.
            if (dragging_window >= 0 || (main_queue != nullptr && main_queue->Count() <= 8)) {
                FlushPointerVisual();
                last_pointer_redraw_tick = now_tick;
            }
        }
        if (dragging_window < 0 && now_tick >= next_system_info_tick) {
            RefreshSystemInfo();
            next_system_info_tick = now_tick + 12;
        }

    }
}
