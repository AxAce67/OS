#include "window.hpp"
#include "font.h"

static bool IsGlyphBitOn(const uint8_t* font_data, int x, int y) {
    if (x < 0 || x >= 8 || y < 0 || y >= 16) {
        return false;
    }
    return ((font_data[y] << x) & 0x80) != 0;
}

// width * height 個のピクセル配列を持つWindowを生成する
Window::Window(int width, int height) : width_(width), height_(height) {
    // newly unlocked 'new' operator!
    data_ = new PixelColor[width * height];
}

Window::~Window() {
    delete[] data_;
}

void Window::DrawPixel(int x, int y, const PixelColor& c) {
    // 範囲外アクセスを防止
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
        // [y * width + x] で1次元配列から2次元の該当ピクセルを特定する
        data_[y * width_ + x] = c;
    }
}

void Window::FillRectangle(int start_x, int start_y, int width, int height, const PixelColor& c) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            DrawPixel(start_x + x, start_y + y, c);
        }
    }
}

void Window::DrawChar(int start_x, int start_y, char c, const PixelColor& color) {
    DrawCharScaled(start_x, start_y, c, color, 1);
}

void Window::DrawCharScaled(int start_x, int start_y, char c, const PixelColor& color, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    const uint8_t* font_data = kFont[(uint8_t)c];
    for (int dy = 0; dy < 16; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
            if (!IsGlyphBitOn(font_data, dx, dy)) {
                continue;
            }
            const int px = start_x + dx * scale;
            const int py = start_y + dy * scale;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    DrawPixel(px + sx, py + sy, color);
                }
            }
        }
    }
}

void Window::DrawString(int start_x, int start_y, const char* str, const PixelColor& color) {
    int x = start_x;
    for (int i = 0; str[i] != '\0'; ++i) {
        DrawChar(x, start_y, str[i], color);
        x += 8; // 1文字進める
    }
}

void Window::SetTransparentColor(const PixelColor& c) {
    has_transparent_color_ = true;
    transparent_color_ = c;
}

void Window::ClearTransparentColor() {
    has_transparent_color_ = false;
}

bool Window::HasTransparentColor() const {
    return has_transparent_color_;
}

const PixelColor& Window::TransparentColor() const {
    return transparent_color_;
}
