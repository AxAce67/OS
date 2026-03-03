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
#include "queue.hpp"
#include "input/message.hpp"
#include "boot_info.h"
#include "memory.hpp"
#include "paging.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "apic.hpp"
#include "timer.hpp"

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

bool StrEqual(const char* a, const char* b) {
    for (int i = 0;; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

bool StrStartsWith(const char* s, const char* prefix) {
    for (int i = 0; prefix[i] != '\0'; ++i) {
        if (s[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

int StrCompare(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        }
        ++i;
    }
    return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
}

bool ContainsChar(const char* s, char ch) {
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] == ch) {
            return true;
        }
    }
    return false;
}

int StrLength(const char* s) {
    int len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

void CopyString(char* dst, const char* src, int max_len) {
    if (max_len <= 0) {
        return;
    }
    int i = 0;
    for (; i < max_len - 1 && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

const char* SkipSpaces(const char* s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

bool NextToken(const char* s, int* pos, char* out, int out_len) {
    int p = *pos;
    while (s[p] == ' ') {
        ++p;
    }
    if (s[p] == '\0') {
        out[0] = '\0';
        *pos = p;
        return false;
    }
    int w = 0;
    while (s[p] != '\0' && s[p] != ' ' && w + 1 < out_len) {
        out[w++] = s[p++];
    }
    out[w] = '\0';
    while (s[p] != '\0' && s[p] != ' ') {
        ++p;
    }
    *pos = p;
    return true;
}

const char* RestOfLine(const char* s, int pos) {
    return SkipSpaces(s + pos);
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
            mouse_cursor->Move(msg.dx, msg.dy);
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

const char* const kBuiltInCommands[] = {
    "help",
    "clear",
    "tick",
    "time",
    "mem",
    "uptime",
    "echo",
    "reboot",
    "history",
    "clearhistory",
    "inputstat",
    "about",
    "pwd",
    "cd",
    "mkdir",
    "touch",
    "write",
    "append",
    "cp",
    "rm",
    "rmdir",
    "mv",
    "ls",
    "stat",
    "cat",
    "repeat",
    "layout",
    "set",
    "alias",
};
const int kBuiltInCommandCount = sizeof(kBuiltInCommands) / sizeof(kBuiltInCommands[0]);

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

    if (StrEqual(cmd, "help")) {
        console->PrintLine("help: core  help clear tick time mem uptime echo reboot");
        console->PrintLine("help: fs1   pwd cd mkdir touch write append cp");
        console->PrintLine("help: fs2   rm rmdir mv ls stat cat");
        console->PrintLine("help: misc  history clearhistory inputstat about");
        console->PrintLine("help: cfg   repeat layout set alias");
        return;
    }

    if (StrEqual(cmd, "about")) {
        console->PrintLine("Native OS shell");
        console->PrintLine("arch: x86_64 / mode: kernel");
        return;
    }

    if (StrEqual(cmd, "pwd")) {
        console->PrintLine(g_cwd);
        return;
    }

    if (StrEqual(cmd, "cd")) {
        InitializeDirectories();
        if (rest[0] == '\0') {
            CopyString(g_cwd, "/", sizeof(g_cwd));
            return;
        }
        char resolved[96];
        if (!ResolvePath(g_cwd, rest, resolved, sizeof(resolved))) {
            console->PrintLine("cd: invalid path");
            return;
        }
        if (!DirectoryExists(resolved)) {
            console->Print("cd: no such directory: ");
            console->PrintLine(rest);
            return;
        }
        CopyString(g_cwd, resolved, sizeof(g_cwd));
        return;
    }

    if (StrEqual(cmd, "mkdir")) {
        InitializeDirectories();
        char name[64];
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->PrintLine("mkdir: directory required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("mkdir: too many arguments");
            return;
        }
        char resolved[96];
        if (!ResolvePath(g_cwd, name, resolved, sizeof(resolved))) {
            console->PrintLine("mkdir: invalid path");
            return;
        }
        if (StrEqual(resolved, "/")) {
            console->PrintLine("mkdir: already exists: /");
            return;
        }
        if (DirectoryExists(resolved)) {
            console->Print("mkdir: already exists: ");
            console->PrintLine(resolved);
            return;
        }
        char parent[96];
        if (!GetParentPath(resolved, parent, sizeof(parent))) {
            console->PrintLine("mkdir: invalid parent");
            return;
        }
        if (!DirectoryExists(parent)) {
            console->Print("mkdir: parent missing: ");
            console->PrintLine(parent);
            return;
        }
        if (!CreateDirectory(resolved)) {
            console->PrintLine("mkdir: table full");
            return;
        }
        return;
    }

    if (StrEqual(cmd, "touch")) {
        char name[64];
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->PrintLine("touch: filename required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("touch: too many arguments");
            return;
        }
        char resolved_path[96];
        if (!ResolveFilePath(g_cwd, name, resolved_path, sizeof(resolved_path))) {
            console->PrintLine("touch: invalid path");
            return;
        }
        if (FindShellFileByAbsPath(resolved_path) != nullptr) {
            return;
        }
        if (FindBootFileByPath(g_cwd, name) != nullptr) {
            console->PrintLine("touch: read-only boot file exists");
            return;
        }
        if (CreateShellFile(resolved_path) == nullptr) {
            console->PrintLine("touch: file table full");
            return;
        }
        return;
    }

    if (StrEqual(cmd, "write") || StrEqual(cmd, "append")) {
        char name[64];
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->Print(StrEqual(cmd, "write") ? "write: filename required\n" : "append: filename required\n");
            return;
        }
        const char* text = RestOfLine(command, pos);
        char resolved_path[96];
        if (!ResolveFilePath(g_cwd, name, resolved_path, sizeof(resolved_path))) {
            console->Print(StrEqual(cmd, "write") ? "write: invalid path\n" : "append: invalid path\n");
            return;
        }
        if (FindBootFileByPath(g_cwd, name) != nullptr) {
            console->Print(StrEqual(cmd, "write") ? "write: boot file is read-only\n" : "append: boot file is read-only\n");
            return;
        }
        ShellFile* file = FindShellFileByAbsPathMutable(resolved_path);
        if (file == nullptr) {
            file = CreateShellFile(resolved_path);
            if (file == nullptr) {
                console->PrintLine("write: file table full");
                return;
            }
        }

        const int text_len = StrLength(text);
        if (StrEqual(cmd, "write")) {
            const int max_write = static_cast<int>(sizeof(file->data));
            int copy_len = (text_len < max_write) ? text_len : max_write;
            for (int i = 0; i < copy_len; ++i) {
                file->data[i] = static_cast<uint8_t>(text[i]);
            }
            file->size = static_cast<uint64_t>(copy_len);
            if (copy_len < text_len) {
                console->PrintLine("write: truncated");
            }
            return;
        }

        const int max_size = static_cast<int>(sizeof(file->data));
        int cur_size = static_cast<int>(file->size);
        if (cur_size >= max_size) {
            console->PrintLine("append: file full");
            return;
        }
        int writable = max_size - cur_size;
        int copy_len = (text_len < writable) ? text_len : writable;
        for (int i = 0; i < copy_len; ++i) {
            file->data[cur_size + i] = static_cast<uint8_t>(text[i]);
        }
        file->size = static_cast<uint64_t>(cur_size + copy_len);
        if (copy_len < text_len) {
            console->PrintLine("append: truncated");
        }
        return;
    }

    if (StrEqual(cmd, "cp")) {
        char src_name[64];
        char dst_name[64];
        if (!NextToken(command, &pos, src_name, sizeof(src_name)) ||
            !NextToken(command, &pos, dst_name, sizeof(dst_name))) {
            console->PrintLine("cp: src and dst required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("cp: too many arguments");
            return;
        }

        char dst_path[96];
        if (!ResolveFilePath(g_cwd, dst_name, dst_path, sizeof(dst_path))) {
            console->PrintLine("cp: invalid destination");
            return;
        }
        if (DirectoryExists(dst_path)) {
            console->PrintLine("cp: destination is directory");
            return;
        }
        if (FindBootFileByPath("/", dst_path) != nullptr && FindShellFileByAbsPath(dst_path) == nullptr) {
            console->PrintLine("cp: destination conflicts with boot file");
            return;
        }

        const ShellFile* src_user_file = FindShellFileByPath(g_cwd, src_name);
        const BootFileEntry* src_boot_file = nullptr;
        if (src_user_file == nullptr) {
            src_boot_file = FindBootFileByPath(g_cwd, src_name);
        }
        if (src_user_file == nullptr && src_boot_file == nullptr) {
            console->Print("cp: not found: ");
            console->PrintLine(src_name);
            return;
        }

        uint64_t src_size = 0;
        const uint8_t* src_data = nullptr;
        if (src_user_file != nullptr) {
            src_size = src_user_file->size;
            src_data = src_user_file->data;
        } else {
            src_size = src_boot_file->size;
            src_data = src_boot_file->data;
        }

        ShellFile* dst_file = FindShellFileByAbsPathMutable(dst_path);
        if (dst_file == nullptr) {
            dst_file = CreateShellFile(dst_path);
            if (dst_file == nullptr) {
                console->PrintLine("cp: file table full");
                return;
            }
        }

        const uint64_t max_size = sizeof(dst_file->data);
        uint64_t copy_len = (src_size < max_size) ? src_size : max_size;
        for (uint64_t i = 0; i < copy_len; ++i) {
            dst_file->data[i] = src_data[i];
        }
        dst_file->size = copy_len;
        if (copy_len < src_size) {
            console->PrintLine("cp: truncated");
        }
        return;
    }

    if (StrEqual(cmd, "rm")) {
        char target[64];
        if (!NextToken(command, &pos, target, sizeof(target))) {
            console->PrintLine("rm: path required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("rm: too many arguments");
            return;
        }
        char resolved_path[96];
        if (!ResolvePath(g_cwd, target, resolved_path, sizeof(resolved_path))) {
            console->PrintLine("rm: invalid path");
            return;
        }
        if (StrEqual(resolved_path, "/")) {
            console->PrintLine("rm: cannot remove /");
            return;
        }
        if (IsPathSameOrChild(g_cwd, resolved_path)) {
            console->PrintLine("rm: path is in use");
            return;
        }

        ShellFile* file = FindShellFileByAbsPathMutable(resolved_path);
        if (file != nullptr) {
            file->used = false;
            file->path[0] = '\0';
            file->size = 0;
            return;
        }

        if (FindBootFileByPath(g_cwd, target) != nullptr) {
            console->PrintLine("rm: boot file is read-only");
            return;
        }

        ShellDir* dir = FindDirectoryMutable(resolved_path);
        if (dir != nullptr) {
            if (!IsDirectoryEmpty(resolved_path)) {
                console->PrintLine("rm: directory not empty");
                return;
            }
            dir->used = false;
            dir->path[0] = '\0';
            return;
        }

        console->Print("rm: not found: ");
        console->PrintLine(target);
        return;
    }

    if (StrEqual(cmd, "rmdir")) {
        char target[64];
        if (!NextToken(command, &pos, target, sizeof(target))) {
            console->PrintLine("rmdir: path required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("rmdir: too many arguments");
            return;
        }
        char resolved_path[96];
        if (!ResolvePath(g_cwd, target, resolved_path, sizeof(resolved_path))) {
            console->PrintLine("rmdir: invalid path");
            return;
        }
        if (StrEqual(resolved_path, "/")) {
            console->PrintLine("rmdir: cannot remove /");
            return;
        }
        if (IsPathSameOrChild(g_cwd, resolved_path)) {
            console->PrintLine("rmdir: path is in use");
            return;
        }
        ShellDir* dir = FindDirectoryMutable(resolved_path);
        if (dir == nullptr) {
            if (FindBootFileByPath(g_cwd, target) != nullptr || FindShellFileByPath(g_cwd, target) != nullptr) {
                console->PrintLine("rmdir: not a directory");
            } else {
                console->Print("rmdir: not found: ");
                console->PrintLine(target);
            }
            return;
        }
        if (!IsDirectoryEmpty(resolved_path)) {
            console->PrintLine("rmdir: directory not empty");
            return;
        }
        dir->used = false;
        dir->path[0] = '\0';
        return;
    }

    if (StrEqual(cmd, "mv")) {
        char src_name[64];
        char dst_name[64];
        if (!NextToken(command, &pos, src_name, sizeof(src_name)) ||
            !NextToken(command, &pos, dst_name, sizeof(dst_name))) {
            console->PrintLine("mv: src and dst required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("mv: too many arguments");
            return;
        }

        char src_path[96];
        char dst_path[96];
        if (!ResolvePath(g_cwd, src_name, src_path, sizeof(src_path)) ||
            !ResolvePath(g_cwd, dst_name, dst_path, sizeof(dst_path))) {
            console->PrintLine("mv: invalid path");
            return;
        }
        if (StrEqual(src_path, "/")) {
            console->PrintLine("mv: cannot move /");
            return;
        }

        char dst_parent[96];
        if (!GetParentPath(dst_path, dst_parent, sizeof(dst_parent)) || !DirectoryExists(dst_parent)) {
            console->PrintLine("mv: destination parent missing");
            return;
        }
        if (DirectoryExists(dst_path) || FindShellFileByAbsPath(dst_path) != nullptr) {
            console->PrintLine("mv: destination exists");
            return;
        }
        if (FindBootFileByPath("/", dst_path) != nullptr) {
            console->PrintLine("mv: destination conflicts with boot file");
            return;
        }

        ShellFile* src_file = FindShellFileByAbsPathMutable(src_path);
        if (src_file != nullptr) {
            src_file->path[0] = '\0';
            CopyString(src_file->path, dst_path, sizeof(src_file->path));
            return;
        }

        if (FindBootFileByPath("/", src_path) != nullptr) {
            console->PrintLine("mv: boot file is read-only");
            return;
        }

        ShellDir* src_dir = FindDirectoryMutable(src_path);
        if (src_dir == nullptr) {
            console->Print("mv: not found: ");
            console->PrintLine(src_name);
            return;
        }
        if (IsPathSameOrChild(dst_path, src_path)) {
            console->PrintLine("mv: invalid destination");
            return;
        }

        char new_path[96];
        for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
            if (!g_dirs[i].used || !IsPathSameOrChild(g_dirs[i].path, src_path)) {
                continue;
            }
            if (!BuildMovedPath(g_dirs[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
                console->PrintLine("mv: path too long");
                return;
            }
            if (DirectoryExistsOutsideMove(new_path, src_path) ||
                ShellFileExistsOutsideMove(new_path, src_path)) {
                console->PrintLine("mv: destination exists");
                return;
            }
        }
        for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
            if (!g_files[i].used || !IsPathSameOrChild(g_files[i].path, src_path)) {
                continue;
            }
            if (!BuildMovedPath(g_files[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
                console->PrintLine("mv: path too long");
                return;
            }
            if (DirectoryExistsOutsideMove(new_path, src_path) ||
                ShellFileExistsOutsideMove(new_path, src_path)) {
                console->PrintLine("mv: destination exists");
                return;
            }
            if (FindBootFileByPath("/", new_path) != nullptr) {
                console->PrintLine("mv: destination conflicts with boot file");
                return;
            }
        }

        for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
            if (!g_dirs[i].used) {
                continue;
            }
            if (!IsPathSameOrChild(g_dirs[i].path, src_path)) {
                continue;
            }
            if (!BuildMovedPath(g_dirs[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
                console->PrintLine("mv: path too long");
                return;
            }
            CopyString(g_dirs[i].path, new_path, sizeof(g_dirs[i].path));
        }
        for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
            if (!g_files[i].used) {
                continue;
            }
            if (!IsPathSameOrChild(g_files[i].path, src_path)) {
                continue;
            }
            if (!BuildMovedPath(g_files[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
                console->PrintLine("mv: path too long");
                return;
            }
            CopyString(g_files[i].path, new_path, sizeof(g_files[i].path));
        }
        if (IsPathSameOrChild(g_cwd, src_path) &&
            BuildMovedPath(g_cwd, src_path, dst_path, new_path, sizeof(new_path))) {
            CopyString(g_cwd, new_path, sizeof(g_cwd));
        }
        return;
    }

    if (StrEqual(cmd, "clear")) {
        console->Clear();
        return;
    }

    if (StrEqual(cmd, "tick") || StrEqual(cmd, "time")) {
        console->Print("ticks=");
        console->PrintDec(static_cast<int64_t>(CurrentTick()));
        console->Print("\n");
        return;
    }

    if (StrEqual(cmd, "uptime")) {
        console->Print("uptime_ticks=");
        console->PrintDec(static_cast<int64_t>(CurrentTick()));
        console->Print("\n");
        return;
    }

    if (StrEqual(cmd, "mem")) {
        uint64_t free_pages = memory_manager->CountFreePages();
        uint64_t free_mib = (free_pages * kPageSize) / kMiB;
        console->Print("free_ram=");
        console->PrintDec(static_cast<int64_t>(free_mib));
        console->PrintLine(" MiB");
        return;
    }

    if (StrEqual(cmd, "inputstat")) {
        console->Print("kbd_dropped=");
        console->PrintDec(static_cast<int64_t>(g_keyboard_dropped_events));
        console->Print(" mouse_dropped=");
        console->PrintDec(static_cast<int64_t>(g_mouse_dropped_events));
        console->Print("\n");
        return;
    }

    if (StrEqual(cmd, "repeat")) {
        if (rest[0] == '\0') {
            console->Print("repeat=");
            console->PrintLine(g_key_repeat_enabled ? "on" : "off");
            return;
        }
        if (StrEqual(rest, "on")) {
            g_key_repeat_enabled = true;
            console->PrintLine("repeat=on");
            return;
        }
        if (StrEqual(rest, "off")) {
            g_key_repeat_enabled = false;
            console->PrintLine("repeat=off");
            return;
        }
    }

    if (StrEqual(cmd, "layout")) {
        if (rest[0] == '\0') {
            console->Print("layout=");
            console->PrintLine(g_jp_layout ? "jp" : "us");
            return;
        }
        if (StrEqual(rest, "us")) {
            g_jp_layout = false;
            console->PrintLine("layout=us");
            return;
        }
        if (StrEqual(rest, "jp")) {
            g_jp_layout = true;
            console->PrintLine("layout=jp");
            return;
        }
    }

    if (StrEqual(cmd, "set")) {
        char key[32];
        if (!NextToken(command, &pos, key, sizeof(key))) {
            PrintPairs("vars:", g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])));
            return;
        }
        ShellPair* p = EnsurePair(g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])), key);
        if (p == nullptr) {
            console->PrintLine("set: table full");
            return;
        }
        CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
        console->Print("set ");
        console->Print(p->key);
        console->Print("=");
        console->PrintLine(p->value);
        return;
    }

    if (StrEqual(cmd, "alias")) {
        char key[32];
        if (!NextToken(command, &pos, key, sizeof(key))) {
            PrintPairs("aliases:", g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])));
            return;
        }
        ShellPair* p = EnsurePair(g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])), key);
        if (p == nullptr) {
            console->PrintLine("alias: table full");
            return;
        }
        CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
        console->Print("alias ");
        console->Print(p->key);
        console->Print("=");
        console->PrintLine(p->value);
        return;
    }

    if (StrEqual(cmd, "ls")) {
        struct LsEntry {
            char name[64];
            bool is_dir;
            uint64_t size;
            int kind; // 0=dir, 1=user file, 2=boot file
        };
        LsEntry entries[128];
        int entry_count = 0;
        auto CopyLsEntry = [&](LsEntry* dst, const LsEntry* src) {
            CopyString(dst->name, src->name, sizeof(dst->name));
            dst->is_dir = src->is_dir;
            dst->size = src->size;
            dst->kind = src->kind;
        };
        auto AddLsEntry = [&](const char* name, bool is_dir, uint64_t size, int kind) {
            if (entry_count >= static_cast<int>(sizeof(entries) / sizeof(entries[0]))) {
                return;
            }
            CopyString(entries[entry_count].name, name, sizeof(entries[entry_count].name));
            entries[entry_count].is_dir = is_dir;
            entries[entry_count].size = size;
            entries[entry_count].kind = kind;
            ++entry_count;
        };

        bool long_format = false;
        char target_arg[64];
        target_arg[0] = '\0';
        int arg_pos = 0;
        char arg[64];
        while (NextToken(rest, &arg_pos, arg, sizeof(arg))) {
            if (arg[0] == '-') {
                if (StrEqual(arg, "-l")) {
                    long_format = true;
                } else {
                    console->Print("ls: unknown option: ");
                    console->PrintLine(arg);
                    return;
                }
                continue;
            }
            if (target_arg[0] != '\0') {
                console->PrintLine("ls: too many paths");
                return;
            }
            CopyString(target_arg, arg, sizeof(target_arg));
        }

        char target_dir[96];
        if (target_arg[0] == '\0') {
            CopyString(target_dir, g_cwd, sizeof(target_dir));
        } else if (!ResolvePath(g_cwd, target_arg, target_dir, sizeof(target_dir))) {
            console->PrintLine("ls: invalid path");
            return;
        }
        if (!DirectoryExists(target_dir)) {
            console->Print("ls: no such directory: ");
            console->PrintLine(target_arg[0] == '\0' ? target_dir : target_arg);
            return;
        }

        for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
            if (!g_dirs[i].used || StrEqual(g_dirs[i].path, "/")) {
                continue;
            }
            char parent[96];
            if (!GetParentPath(g_dirs[i].path, parent, sizeof(parent))) {
                continue;
            }
            if (!StrEqual(parent, target_dir)) {
                continue;
            }
            char base[64];
            GetBaseName(g_dirs[i].path, base, sizeof(base));
            AddLsEntry(base, true, 0, 0);
        }

        for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
            if (!g_files[i].used) {
                continue;
            }
            char parent[96];
            if (!GetParentPath(g_files[i].path, parent, sizeof(parent))) {
                continue;
            }
            if (!StrEqual(parent, target_dir)) {
                continue;
            }
            char base[64];
            GetBaseName(g_files[i].path, base, sizeof(base));
            AddLsEntry(base, false, g_files[i].size, 1);
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
                if (!GetParentPath(abs_file_path, parent, sizeof(parent))) {
                    continue;
                }
                if (!StrEqual(parent, target_dir)) {
                    continue;
                }

                char base[64];
                GetBaseName(abs_file_path, base, sizeof(base));
                AddLsEntry(base, false, fs->files[i].size, 2);
            }
        }

        for (int i = 1; i < entry_count; ++i) {
            LsEntry key;
            CopyLsEntry(&key, &entries[i]);
            int j = i - 1;
            while (j >= 0) {
                int cmp = StrCompare(entries[j].name, key.name);
                if (cmp < 0) {
                    break;
                }
                if (cmp == 0) {
                    if (entries[j].is_dir && !key.is_dir) {
                        break;
                    }
                    if (entries[j].is_dir == key.is_dir && entries[j].kind <= key.kind) {
                        break;
                    }
                }
                CopyLsEntry(&entries[j + 1], &entries[j]);
                --j;
            }
            CopyLsEntry(&entries[j + 1], &key);
        }

        for (int i = 0; i < entry_count; ++i) {
            if (long_format) {
                if (entries[i].is_dir) {
                    console->Print("drwxr-xr-x ");
                    console->PrintDec(0);
                    console->Print(" B ");
                    console->Print(entries[i].name);
                    console->PrintLine("/");
                } else if (entries[i].kind == 1) {
                    console->Print("-rw-rw-r-- ");
                    console->PrintDec(static_cast<int64_t>(entries[i].size));
                    console->Print(" B ");
                    console->PrintLine(entries[i].name);
                } else {
                    console->Print("-rw-r--r-- ");
                    console->PrintDec(static_cast<int64_t>(entries[i].size));
                    console->Print(" B ");
                    console->PrintLine(entries[i].name);
                }
            } else {
                console->Print(entries[i].name);
                if (entries[i].is_dir) {
                    console->Print("/");
                }
                console->Print("\n");
            }
        }
        return;
    }

    if (StrEqual(cmd, "stat")) {
        char name[64];
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->PrintLine("stat: filename required");
            return;
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("stat: too many arguments");
            return;
        }
        const ShellFile* user_file = FindShellFileByPath(g_cwd, name);
        if (user_file != nullptr) {
            PrintShellFileStat(user_file);
            return;
        }
        const BootFileEntry* boot_file = FindBootFileByPath(g_cwd, name);
        if (boot_file == nullptr) {
            console->Print("stat: not found: ");
            console->PrintLine(name);
            return;
        }
        PrintBootFileStat(boot_file);
        return;
    }

    if (StrEqual(cmd, "cat")) {
        bool show_line_number = false;
        bool paged = false;
        char name[64];
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->PrintLine("cat: filename required");
            return;
        }
        while (name[0] == '-') {
            if (StrEqual(name, "-n")) {
                show_line_number = true;
            } else if (StrEqual(name, "-p")) {
                paged = true;
            } else {
                console->Print("cat: unknown option: ");
                console->PrintLine(name);
                return;
            }
            if (!NextToken(command, &pos, name, sizeof(name))) {
                console->PrintLine("cat: filename required");
                return;
            }
        }
        char extra[8];
        if (NextToken(command, &pos, extra, sizeof(extra))) {
            console->PrintLine("cat: too many arguments");
            return;
        }
        const ShellFile* user_file = FindShellFileByPath(g_cwd, name);
        if (user_file != nullptr) {
            BootFileEntry temp;
            temp.size = user_file->size;
            temp.data = const_cast<uint8_t*>(user_file->data);
            if (paged) {
                PrintBootFilePaged(&temp, show_line_number);
            } else if (show_line_number) {
                PrintBootFileNumbered(&temp);
            } else {
                PrintBootFile(&temp);
            }
            return;
        }
        const BootFileEntry* boot_file = FindBootFileByPath(g_cwd, name);
        if (boot_file == nullptr) {
            console->Print("cat: not found: ");
            console->PrintLine(name);
            return;
        }
        if (paged) {
            PrintBootFilePaged(boot_file, show_line_number);
        } else if (show_line_number) {
            PrintBootFileNumbered(boot_file);
        } else {
            PrintBootFile(boot_file);
        }
        return;
    }

    if (StrEqual(cmd, "echo")) {
        console->PrintLine(rest);
        return;
    }

    if (StrEqual(cmd, "reboot")) {
        console->PrintLine("rebooting...");
        Reboot();
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
        console->PrintHex(xhci.mmio_base, 8);
        console->Print(")\n");
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
    uint8_t last_make_key = 0;
    bool last_make_valid = false;
    int input_row = console->CursorRow();
    int input_col = console->CursorColumn();
    int rendered_len = 0;
    auto RefreshConsole = [&]() {
        layer_manager->Draw(0, 0, console->PixelWidth(), console->PixelHeight());
    };
    auto EnsureLiveConsole = [&]() {
        if (console->IsScrolled()) {
            console->ResetScroll();
            RefreshConsole();
        }
    };

    auto RenderInputLine = [&]() {
        const int clear_len = console->Columns() - input_col - 1;
        console->SetCursorPosition(input_row, input_col);
        for (int i = 0; i < clear_len; ++i) {
            console->Print(" ");
        }
        console->SetCursorPosition(input_row, input_col);
        console->Print(command_buffer);
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
        RenderInputLine();
        RefreshConsole();
    };

    auto BackspaceAtCursor = [&]() {
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
        RefreshConsole();
    };

    auto DeleteAtCursor = [&]() {
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
            RefreshConsole();
            return;
        }
        // ユーザー体験優先: 行末でDeleteが押されたらBackspace相当で1文字消す
        if (cursor_pos == command_len) {
            BackspaceAtCursor();
        }
    };

    auto HandleTabCompletion = [&]() {
        if (command_len == 0 || ContainsChar(command_buffer, ' ')) {
            return;
        }

        int match_count = 0;
        const char* single_match = nullptr;
        for (int i = 0; i < kBuiltInCommandCount; ++i) {
            if (StrStartsWith(kBuiltInCommands[i], command_buffer)) {
                ++match_count;
                single_match = kBuiltInCommands[i];
            }
        }

        if (match_count == 0) {
            return;
        }

        if (match_count == 1 && single_match != nullptr) {
            ReplaceInputLine(single_match);
            return;
        }

        console->Print("\n");
        for (int i = 0; i < kBuiltInCommandCount; ++i) {
            if (StrStartsWith(kBuiltInCommands[i], command_buffer)) {
                console->Print(kBuiltInCommands[i]);
                console->Print(" ");
            }
        }
        console->Print("\n");
        PrintPrompt();
        input_row = console->CursorRow();
        input_col = console->CursorColumn();
        rendered_len = 0;
        cursor_pos = command_len;
        RenderInputLine();
        RefreshConsole();
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
            // キューが空ならば、割り込みを許可(sti)すると同時にCPUを休止(hlt)させる
            __asm__ volatile("sti\n\thlt");
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
                if (msg.pointer_mode == Message::PointerMode::kAbsolute) {
                    mouse_cursor->SetPosition(msg.x, msg.y);
                } else {
                    mouse_cursor->Move(msg.dx, msg.dy);
                }
                if (msg.wheel > 0) {
                    console->ScrollUp(msg.wheel * 3);
                    RefreshConsole();
                } else if (msg.wheel < 0) {
                    console->ScrollDown((-msg.wheel) * 3);
                    RefreshConsole();
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
                            --cursor_pos;
                            RenderInputLine();
                            RefreshConsole();
                        }
                    } else if ((ext & 0x7F) == 0x4D) { // Arrow Right
                        EnsureLiveConsole();
                        if (cursor_pos < command_len) {
                            ++cursor_pos;
                            RenderInputLine();
                            RefreshConsole();
                        }
                    } else if ((ext & 0x7F) == 0x47) { // Home
                        EnsureLiveConsole();
                        cursor_pos = 0;
                        RenderInputLine();
                        RefreshConsole();
                    } else if ((ext & 0x7F) == 0x4F) { // End
                        EnsureLiveConsole();
                        cursor_pos = command_len;
                        RenderInputLine();
                        RefreshConsole();
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
                    if (last_make_valid && keyup == last_make_key) {
                        last_make_valid = false;
                    }
                    break;
                }
                if ((msg.keycode & 0x80) == 0) {
                    const uint8_t key = msg.keycode & 0x7F;
                    if (!g_key_repeat_enabled && last_make_valid && key == last_make_key) {
                        break;
                    }
                    last_make_key = key;
                    last_make_valid = true;
                    if (IsCtrlPressed(keyboard_state)) {
                        if (key == 0x1E) { // Ctrl + A
                            EnsureLiveConsole();
                            cursor_pos = 0;
                            RenderInputLine();
                            RefreshConsole();
                            break;
                        }
                        if (key == 0x12) { // Ctrl + E
                            EnsureLiveConsole();
                            cursor_pos = command_len;
                            RenderInputLine();
                            RefreshConsole();
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
                            history_nav = -1;
                            draft_buffer[0] = '\0';
                            RenderInputLine();
                            RefreshConsole();
                            break;
                        }
                    }
                    if (key == 0x47) { // Home (non-E0 fallback)
                        EnsureLiveConsole();
                        cursor_pos = 0;
                        RenderInputLine();
                        RefreshConsole();
                        break;
                    }
                    if (key == 0x48) { // Arrow Up (non-E0 fallback)
                        EnsureLiveConsole();
                        BrowseHistoryUp();
                        break;
                    }
                    if (key == 0x4F) { // End (non-E0 fallback)
                        EnsureLiveConsole();
                        cursor_pos = command_len;
                        RenderInputLine();
                        RefreshConsole();
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
                            history_nav = -1;
                            draft_buffer[0] = '\0';
                            PrintPrompt();
                            input_row = console->CursorRow();
                            input_col = console->CursorColumn();
                            console->SetCursorPosition(input_row, input_col);
                        } else if (IsPrintableAscii(ch) &&
                                   command_len < MaxInputLen()) {
                            for (int i = command_len; i > cursor_pos; --i) {
                                command_buffer[i] = command_buffer[i - 1];
                            }
                            command_buffer[cursor_pos] = ch;
                            ++command_len;
                            ++cursor_pos;
                            command_buffer[command_len] = '\0';
                            RenderInputLine();
                        }
                        RefreshConsole();
                    }
                }
                break;
            default:
                break;
        }

    }
}
