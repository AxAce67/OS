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
      rows_{(window->Height() - (kMarginY * 2)) / kCellHeight},
      columns_{(window->Width() - (kMarginX * 2)) / kCellWidth},
      cursor_row_{0}, cursor_column_{0},
      scrollback_head_{0}, scrollback_count_{0}, view_offset_{0} {
    if (rows_ < 1) rows_ = 1;
    if (rows_ > kMaxRows) rows_ = kMaxRows;
    if (columns_ < 1) columns_ = 1;
    if (columns_ > kMaxColumns) columns_ = kMaxColumns;
    memset(buffer_, 0, sizeof(buffer_));
    memset(scrollback_, 0, sizeof(scrollback_));
}

void Console::Print(const char* s) {
    while (*s) {
        if (*s == '\n') {
            Newline();
        } else {
            if (cursor_column_ >= columns_) {
                Newline();
            }
            buffer_[cursor_row_][cursor_column_] = *s;
            if (view_offset_ == 0) {
                // 文字セルを背景色で消してから文字を描く。
                // これをしないと space 描画時に既存ピクセルが残って重なって見える。
                window_->FillRectangle(PixelX(cursor_column_), PixelY(cursor_row_),
                                       kCellWidth, kCellHeight, {bg_r_, bg_g_, bg_b_});
                window_->DrawCharScaled(PixelX(cursor_column_), PixelY(cursor_row_),
                                        *s, {fg_r_, fg_g_, fg_b_}, kFontScale);
            }
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

void Console::Clear() {
    window_->FillRectangle(0, 0, window_->Width(), window_->Height(), {bg_r_, bg_g_, bg_b_});
    memset(buffer_, 0, sizeof(buffer_));
    memset(scrollback_, 0, sizeof(scrollback_));
    cursor_row_ = 0;
    cursor_column_ = 0;
    scrollback_head_ = 0;
    scrollback_count_ = 0;
    view_offset_ = 0;
}

bool Console::Backspace() {
    if (cursor_column_ == 0) {
        return false;
    }
    --cursor_column_;
    buffer_[cursor_row_][cursor_column_] = '\0';
    if (view_offset_ == 0) {
        window_->FillRectangle(PixelX(cursor_column_), PixelY(cursor_row_),
                               kCellWidth, kCellHeight, {bg_r_, bg_g_, bg_b_});
    }
    return true;
}

int Console::CursorRow() const {
    return cursor_row_;
}

int Console::CursorColumn() const {
    return cursor_column_;
}

int Console::Rows() const {
    return rows_;
}

int Console::Columns() const {
    return columns_;
}

int Console::PixelWidth() const {
    return window_->Width();
}

int Console::PixelHeight() const {
    return window_->Height();
}

void Console::SetCursorPosition(int row, int column) {
    if (row < 0) row = 0;
    if (row >= rows_) row = rows_ - 1;
    if (column < 0) column = 0;
    if (column >= columns_) column = columns_ - 1;
    cursor_row_ = row;
    cursor_column_ = column;
}

void Console::ScrollUp(int lines) {
    if (lines <= 0 || scrollback_count_ <= 0) {
        return;
    }
    int max_offset = scrollback_count_;
    view_offset_ += lines;
    if (view_offset_ > max_offset) {
        view_offset_ = max_offset;
    }
    RenderVisible();
}

void Console::ScrollDown(int lines) {
    if (lines <= 0 || view_offset_ <= 0) {
        return;
    }
    view_offset_ -= lines;
    if (view_offset_ < 0) {
        view_offset_ = 0;
    }
    RenderVisible();
}

void Console::ResetScroll() {
    if (view_offset_ == 0) {
        return;
    }
    view_offset_ = 0;
    RenderVisible();
}

bool Console::IsScrolled() const {
    return view_offset_ > 0;
}

void Console::Newline() {
    cursor_column_ = 0;
    if (cursor_row_ < rows_ - 1) {
        ++cursor_row_;
    } else {
        Scroll();
    }
}

void Console::Scroll() {
    int slot = 0;
    if (scrollback_count_ < kScrollbackLines) {
        slot = (scrollback_head_ + scrollback_count_) % kScrollbackLines;
        ++scrollback_count_;
    } else {
        slot = scrollback_head_;
        scrollback_head_ = (scrollback_head_ + 1) % kScrollbackLines;
    }
    memcpy(scrollback_[slot], buffer_[0], columns_ + 1);
    if (view_offset_ > 0 && view_offset_ < scrollback_count_) {
        ++view_offset_;
    }

    // バッファを1行ずつ上にずらす
    for (int row = 0; row < rows_ - 1; ++row) {
        memcpy(buffer_[row], buffer_[row + 1], columns_ + 1);
    }

    // 最後の行をクリアする
    memset(buffer_[rows_ - 1], 0, columns_ + 1);
    RenderVisible();
}

void Console::RenderVisible() {
    window_->FillRectangle(0, 0, window_->Width(), window_->Height(), {bg_r_, bg_g_, bg_b_});

    const int total_lines = scrollback_count_ + rows_;
    int top_line = total_lines - rows_ - view_offset_;
    if (top_line < 0) {
        top_line = 0;
    }

    for (int row = 0; row < rows_; ++row) {
        const int virtual_line = top_line + row;
        const char* src = nullptr;
        if (virtual_line < scrollback_count_) {
            const int idx = (scrollback_head_ + virtual_line) % kScrollbackLines;
            src = scrollback_[idx];
        } else {
            const int buf_row = virtual_line - scrollback_count_;
            if (buf_row < 0 || buf_row >= rows_) {
                continue;
            }
            src = buffer_[buf_row];
        }

        for (int col = 0; col < columns_; ++col) {
            if (src[col] != '\0') {
                window_->DrawCharScaled(PixelX(col), PixelY(row),
                                        src[col], {fg_r_, fg_g_, fg_b_}, kFontScale);
            }
        }
    }
}

int Console::PixelX(int column) const {
    return kMarginX + (column * kCellWidth);
}

int Console::PixelY(int row) const {
    return kMarginY + (row * kCellHeight);
}
