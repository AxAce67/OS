#pragma once
#include <stdint.h>
#include "frame_buffer_config.h"

#include "window.hpp"

class Console {
public:
    static const int kRows = 25;
    static const int kColumns = 80;

    Console(Window* window,
            uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
            uint8_t bg_r, uint8_t bg_g, uint8_t bg_b);
    
    // 文字列を出力（必要なら改行やスクロールも自動で行う）
    void Print(const char* s);
    void PrintHex(uint64_t value, int num_digits);
    void PrintDec(int64_t value);
    void PrintLine(const char* s);

private:
    void Newline();
    void Scroll();

    Window* window_;
    uint8_t fg_r_, fg_g_, fg_b_;
    uint8_t bg_r_, bg_g_, bg_b_;
    int cursor_row_, cursor_column_;
    char buffer_[kRows][kColumns + 1]; // 画面の内容を記憶しておくためのバッファ
};

// kernel.cpp等で定義されている文字描画関数を呼び出せるようにする
void DrawChar(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, char c, uint8_t r, uint8_t g, uint8_t b);
void DrawPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
