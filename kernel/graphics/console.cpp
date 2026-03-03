#include "console.hpp"

// C++標準ライブラリがないため、簡易的な memcpy, memset を自作
static void* memset(void* s, int c, uint32_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void* memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

Console::Console(Window* window,
                 uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
                 uint8_t bg_r, uint8_t bg_g, uint8_t bg_b)
    : window_{window},
      fg_r_{fg_r}, fg_g_{fg_g}, fg_b_{fg_b},
      bg_r_{bg_r}, bg_g_{bg_g}, bg_b_{bg_b},
      cursor_row_{0}, cursor_column_{0} {
    memset(buffer_, 0, sizeof(buffer_));
}

void Console::Print(const char* s) {
    while (*s) {
        if (*s == '\n') {
            Newline();
        } else {
            if (cursor_column_ >= kColumns) {
                Newline();
            }
            window_->DrawChar(8 * cursor_column_, 16 * cursor_row_, *s, {fg_r_, fg_g_, fg_b_});
            buffer_[cursor_row_][cursor_column_] = *s;
            ++cursor_column_;
        }
        ++s;
    }
}

void Console::PrintLine(const char* s) {
    Print(s);
    Print("\n");
}

void Console::PrintHex(uint64_t value, int num_digits) {
    char buf[32];
    int pos = 0;
    
    // 0xプレフィックス
    buf[pos++] = '0';
    buf[pos++] = 'x';

    // 指定桁数に合わせる
    for (int i = num_digits - 1; i >= 0; --i) {
        uint8_t nibble = (value >> (i * 4)) & 0x0F;
        buf[pos++] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buf[pos] = '\0';
    Print(buf);
}

void Console::PrintDec(int64_t value) {
    char buf[32];
    int pos = 0;
    if (value == 0) {
        Print("0");
        return;
    }

    bool is_neg = false;
    if (value < 0) {
        is_neg = true;
        value = -value;
    }

    while (value > 0) {
        buf[pos++] = '0' + (value % 10);
        value /= 10;
    }
    if (is_neg) buf[pos++] = '-';

    // 文字列の反転
    for (int i = 0; i < pos / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    buf[pos] = '\0';
    Print(buf);
}

void Console::Newline() {
    cursor_column_ = 0;
    if (cursor_row_ < kRows - 1) {
        ++cursor_row_;
    } else {
        Scroll();
    }
}

void Console::Scroll() {
    // 画面全体を背景色で塗りつぶす
    window_->FillRectangle(0, 0, window_->Width(), window_->Height(), {bg_r_, bg_g_, bg_b_});

    // バッファを1行ずつ上にずらす
    for (int row = 0; row < kRows - 1; ++row) {
        memcpy(buffer_[row], buffer_[row + 1], kColumns + 1);
        // ずらした内容を描画し直す
        for (int col = 0; col < kColumns; ++col) {
            if (buffer_[row][col] != '\0') {
                window_->DrawChar(8 * col, 16 * row, buffer_[row][col], {fg_r_, fg_g_, fg_b_});
            }
        }
    }

    // 最後の行（kRows - 1）をクリアする
    memset(buffer_[kRows - 1], 0, kColumns + 1);
}
