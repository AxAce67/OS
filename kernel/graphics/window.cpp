#include "window.hpp"
#include "font.h"

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
    if (width <= 0 || height <= 0) {
        return;
    }
    int x0 = start_x;
    int y0 = start_y;
    int x1 = start_x + width;
    int y1 = start_y + height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width_) x1 = width_;
    if (y1 > height_) y1 = height_;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        PixelColor* row = &data_[y * width_ + x0];
        for (int x = x0; x < x1; ++x) {
            *row++ = c;
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
    if (scale == 1 &&
        start_x >= 0 && start_y >= 0 &&
        start_x + 8 <= width_ &&
        start_y + 16 <= height_) {
        for (int dy = 0; dy < 16; ++dy) {
            const uint8_t bits = font_data[dy];
            PixelColor* row = &data_[(start_y + dy) * width_ + start_x];
            for (int dx = 0; dx < 8; ++dx) {
                if (((bits << dx) & 0x80) != 0) {
                    row[dx] = color;
                }
            }
        }
        return;
    }

    for (int dy = 0; dy < 16; ++dy) {
        const uint8_t bits = font_data[dy];
        for (int dx = 0; dx < 8; ++dx) {
            if (((bits << dx) & 0x80) == 0) {
                continue;
            }
            const int px = start_x + dx * scale;
            const int py = start_y + dy * scale;
            int x0 = px;
            int y0 = py;
            int x1 = px + scale;
            int y1 = py + scale;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > width_) x1 = width_;
            if (y1 > height_) y1 = height_;
            if (x0 >= x1 || y0 >= y1) {
                continue;
            }
            for (int y = y0; y < y1; ++y) {
                PixelColor* row = &data_[y * width_ + x0];
                for (int x = x0; x < x1; ++x) {
                    *row++ = color;
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
