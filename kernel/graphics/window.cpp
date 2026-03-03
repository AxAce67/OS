#include "window.hpp"
#include "font.h"

static uint8_t BlendChannel(uint8_t dst, uint8_t src, uint8_t alpha) {
    const uint32_t a = alpha;
    const uint32_t inv = 255 - a;
    return static_cast<uint8_t>((dst * inv + src * a) / 255);
}

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
    const uint8_t* font_data = kFont[(uint8_t)c];
    for (int dy = 0; dy < 16; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
            if (IsGlyphBitOn(font_data, dx, dy)) {
                DrawPixel(start_x + dx, start_y + dy, color);
                continue;
            }

            // 近傍ピクセルから簡易カバレッジを作ってエッジを滑らかにする。
            int near = 0;
            if (IsGlyphBitOn(font_data, dx - 1, dy)) near += 3;
            if (IsGlyphBitOn(font_data, dx + 1, dy)) near += 3;
            if (IsGlyphBitOn(font_data, dx, dy - 1)) near += 2;
            if (IsGlyphBitOn(font_data, dx, dy + 1)) near += 2;
            if (IsGlyphBitOn(font_data, dx - 1, dy - 1)) near += 1;
            if (IsGlyphBitOn(font_data, dx + 1, dy - 1)) near += 1;
            if (IsGlyphBitOn(font_data, dx - 1, dy + 1)) near += 1;
            if (IsGlyphBitOn(font_data, dx + 1, dy + 1)) near += 1;

            if (near <= 0) {
                continue;
            }

            int x = start_x + dx;
            int y = start_y + dy;
            if (x < 0 || x >= width_ || y < 0 || y >= height_) {
                continue;
            }

            // 0..14 を 0..180 に圧縮。本文字より弱く描く。
            const uint8_t alpha = static_cast<uint8_t>((near * 180) / 14);
            const PixelColor dst = data_[y * width_ + x];
            const PixelColor blended{
                BlendChannel(dst.r, color.r, alpha),
                BlendChannel(dst.g, color.g, alpha),
                BlendChannel(dst.b, color.b, alpha)
            };
            DrawPixel(x, y, blended);
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
