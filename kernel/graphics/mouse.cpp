#include "mouse.hpp"

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
inline void WriteFrameBufferPixel(const FrameBufferConfig& config, int x, int y, const PixelColor& c) {
    const uint32_t index = (static_cast<uint32_t>(y) * config.pixels_per_scan_line +
                            static_cast<uint32_t>(x)) * 4;
    if (config.pixel_format == kPixelRGBResv8BitPerColor) {
        config.frame_buffer[index] = c.r;
        config.frame_buffer[index + 1] = c.g;
        config.frame_buffer[index + 2] = c.b;
    } else {
        config.frame_buffer[index] = c.b;
        config.frame_buffer[index + 1] = c.g;
        config.frame_buffer[index + 2] = c.r;
    }
}

inline PixelColor ReadFrameBufferPixel(const FrameBufferConfig& config, int x, int y) {
    const uint32_t index = (static_cast<uint32_t>(y) * config.pixels_per_scan_line +
                            static_cast<uint32_t>(x)) * 4;
    if (config.pixel_format == kPixelRGBResv8BitPerColor) {
        return PixelColor{config.frame_buffer[index], config.frame_buffer[index + 1], config.frame_buffer[index + 2]};
    }
    return PixelColor{config.frame_buffer[index + 2], config.frame_buffer[index + 1], config.frame_buffer[index]};
}
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
    SaveBackgroundAt(x_, y_);
    saved_generation_ = layer_manager_->DrawGeneration();
    DrawCursorAt(x_, y_);
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

    // Restore old cursor area quickly if no other layer draw happened.
    if (saved_generation_ == layer_manager_->DrawGeneration()) {
        RestoreSavedBackground(old_x, old_y);
    } else {
        layer_manager_->Draw(old_x, old_y, w, h);
    }
    x_ = x;
    y_ = y;
    SaveBackgroundAt(x_, y_);
    saved_generation_ = layer_manager_->DrawGeneration();
    DrawCursorAt(x_, y_);
}

void MouseCursor::Redraw() {
    DrawCursorAt(x_, y_);
}

int MouseCursor::X() const {
    return x_;
}

int MouseCursor::Y() const {
    return y_;
}

void MouseCursor::SaveBackgroundAt(int x, int y) {
    const auto& config = layer_manager_->GetConfig();
    const int res_x = static_cast<int>(config.horizontal_resolution);
    const int res_y = static_cast<int>(config.vertical_resolution);
    for (int cy = 0; cy < kMouseCursorHeight; ++cy) {
        const int vy = y + cy;
        for (int cx = 0; cx < kMouseCursorWidth; ++cx) {
            const int vx = x + cx;
            const int idx = cy * kMouseCursorWidth + cx;
            if (vx < 0 || vx >= res_x || vy < 0 || vy >= res_y) {
                saved_valid_[idx] = false;
                continue;
            }
            saved_bg_[idx] = ReadFrameBufferPixel(config, vx, vy);
            saved_valid_[idx] = true;
        }
    }
}

void MouseCursor::RestoreSavedBackground(int x, int y) {
    const auto& config = layer_manager_->GetConfig();
    const int res_x = static_cast<int>(config.horizontal_resolution);
    const int res_y = static_cast<int>(config.vertical_resolution);
    for (int cy = 0; cy < kMouseCursorHeight; ++cy) {
        const int vy = y + cy;
        if (vy < 0 || vy >= res_y) {
            continue;
        }
        for (int cx = 0; cx < kMouseCursorWidth; ++cx) {
            const int vx = x + cx;
            if (vx < 0 || vx >= res_x) {
                continue;
            }
            const int idx = cy * kMouseCursorWidth + cx;
            if (!saved_valid_[idx]) {
                continue;
            }
            WriteFrameBufferPixel(config, vx, vy, saved_bg_[idx]);
        }
    }
}

void MouseCursor::DrawCursorAt(int x, int y) {
    const auto& config = layer_manager_->GetConfig();
    const int res_x = static_cast<int>(config.horizontal_resolution);
    const int res_y = static_cast<int>(config.vertical_resolution);
    for (int cy = 0; cy < kMouseCursorHeight; ++cy) {
        const int vy = y + cy;
        if (vy < 0 || vy >= res_y) {
            continue;
        }
        for (int cx = 0; cx < kMouseCursorWidth; ++cx) {
            const int vx = x + cx;
            if (vx < 0 || vx >= res_x) {
                continue;
            }
            const char p = mouse_cursor_shape[cy][cx];
            if (p == ' ') {
                continue;
            }
            if (p == '@') {
                WriteFrameBufferPixel(config, vx, vy, PixelColor{1, 1, 1});
            } else {
                WriteFrameBufferPixel(config, vx, vy, PixelColor{255, 255, 255});
            }
        }
    }
}
