#pragma once
#include <stdint.h>
#include "frame_buffer_config.h"

#include "window.hpp"

class Console {
public:
    static const int kFontWidth = 8;
    static const int kFontHeight = 16;
    static const int kFontScale = 1;
    static const int kCellWidth = kFontWidth * kFontScale;
    static const int kCellHeight = kFontHeight * kFontScale;
    static const int kMarginX = 8;
    static const int kMarginY = 8;
    static const int kMaxRows = 200;
    static const int kMaxColumns = 320;
    static const int kScrollbackLines = 512;

    Console(Window* window,
            uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
            uint8_t bg_r, uint8_t bg_g, uint8_t bg_b);
    
    // 文字列を出力（必要なら改行やスクロールも自動で行う）
    void Print(const char* s);
    void PrintHex(uint64_t value, int num_digits);
    void PrintDec(int64_t value);
    void PrintLine(const char* s);
    void Clear();
    bool Backspace();
    int CursorRow() const;
    int CursorColumn() const;
    int Rows() const;
    int Columns() const;
    int PixelWidth() const;
    int PixelHeight() const;
    void SetCursorPosition(int row, int column);
    void ScrollUp(int lines = 1);
    void ScrollDown(int lines = 1);
    void ResetScroll();
    bool IsScrolled() const;

private:
    void Newline();
    void Scroll();
    void RenderVisible();

    Window* window_;
    uint8_t fg_r_, fg_g_, fg_b_;
    uint8_t bg_r_, bg_g_, bg_b_;
    int rows_;
    int columns_;
    int cursor_row_, cursor_column_;
    char buffer_[kMaxRows][kMaxColumns + 1];
    char scrollback_[kScrollbackLines][kMaxColumns + 1];
    int scrollback_head_;
    int scrollback_count_;
    int view_offset_;

    int PixelX(int column) const;
    int PixelY(int row) const;
};

// kernel.cpp等で定義されている文字描画関数を呼び出せるようにする
void DrawChar(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, char c, uint8_t r, uint8_t g, uint8_t b);
void DrawPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
