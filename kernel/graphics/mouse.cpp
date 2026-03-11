#include "mouse.hpp"
#include "boot_info.h"
#include "window.hpp"

// おなじみのマウスカーソルの形
// '@' = 黒（フチ）、'.' = 白（中身）、' ' = 透明
const char mouse_cursor_shape[kMouseCursorHeight][kMouseCursorWidth + 1] = {
    "@              ",
    "@@             ",
    "@.@            ",
    "@..@           ",
    "@...@          ",
    "@....@         ",
    "@.....@        ",
    "@......@       ",
    "@.......@      ",
    "@........@     ",
    "@.........@    ",
    "@..........@   ",
    "@...........@  ",
    "@............@ ",
    "@......@@@@@@@@",
    "@......@       ",
    "@....@@.@      ",
    "@...@ @.@      ",
    "@..@   @.@     ",
    "@.@    @.@     ",
    "@@      @.@    ",
    "@       @.@    ",
    "         @.@   ",
    "         @@@   ",
};

namespace {
const PixelColor kCursorTransparent{255, 0, 255};
}  // namespace

MouseCursor::MouseCursor(unsigned int initial_x, unsigned int initial_y, LayerManager* layer_manager)
    : layer_manager_(layer_manager),
      x_(static_cast<int>(initial_x)),
      y_(static_cast<int>(initial_y)) {
    const auto& config = layer_manager_->GetConfig();
    const int res_x = static_cast<int>(config.horizontal_resolution);
    const int res_y = static_cast<int>(config.vertical_resolution);
    const int max_x = (res_x > kMouseCursorWidth) ? (res_x - kMouseCursorWidth) : 0;
    const int max_y = (res_y > kMouseCursorHeight) ? (res_y - kMouseCursorHeight) : 0;
    if (x_ < 0) x_ = 0;
    if (y_ < 0) y_ = 0;
    if (x_ > max_x) x_ = max_x;
    if (y_ > max_y) y_ = max_y;
    window_ = new Window(kMouseCursorWidth, kMouseCursorHeight);
    window_->SetTransparentColor(kCursorTransparent);
    DrawCursorWindow();
    layer_ = layer_manager_->NewLayer();
    if (layer_ != nullptr) {
        layer_->SetWindow(window_).Move(x_, y_);
        layer_manager_->UpDown(layer_, 255);
    }
}

void MouseCursor::Move(int dx, int dy) {
    SetPosition(x_ + dx, y_ + dy);
}

void MouseCursor::SetPosition(int x, int y) {
    const int old_x = x_;
    const int old_y = y_;
    const int w = kMouseCursorWidth;
    const int h = kMouseCursorHeight;

    const auto& config = layer_manager_->GetConfig();
    int res_x = static_cast<int>(config.horizontal_resolution);
    int res_y = static_cast<int>(config.vertical_resolution);

    const int max_x = (res_x > w) ? (res_x - w) : 0;
    const int max_y = (res_y > h) ? (res_y - h) : 0;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;

    if (x == old_x && y == old_y) {
        return;
    }
    x_ = x;
    y_ = y;
    if (layer_ != nullptr) {
        layer_->Move(x_, y_);
        layer_manager_->UpDown(layer_, 255);
    }
    layer_manager_->Draw(old_x, old_y, w, h);
    if (old_x != x_ || old_y != y_) {
        layer_manager_->Draw(x_, y_, w, h);
    }
}

void MouseCursor::Redraw() {
    if (layer_ != nullptr) {
        layer_manager_->UpDown(layer_, 255);
    }
    layer_manager_->Draw(x_, y_, kMouseCursorWidth, kMouseCursorHeight);
}

int MouseCursor::X() const {
    return x_;
}

int MouseCursor::Y() const {
    return y_;
}

void MouseCursor::DrawCursorWindow() {
    if (window_ == nullptr) {
        return;
    }
    window_->FillRectangle(0, 0, kMouseCursorWidth, kMouseCursorHeight, kCursorTransparent);
    for (int cy = 0; cy < kMouseCursorHeight; ++cy) {
        for (int cx = 0; cx < kMouseCursorWidth; ++cx) {
            const char p = mouse_cursor_shape[cy][cx];
            if (p == ' ') {
                continue;
            }
            if (p == '@') {
                window_->DrawPixel(cx, cy, PixelColor{1, 1, 1});
            } else {
                window_->DrawPixel(cx, cy, PixelColor{255, 255, 255});
            }
        }
    }
}
