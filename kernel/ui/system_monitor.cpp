#include "ui/system_monitor.hpp"

#include "graphics/layer.hpp"
#include "graphics/window.hpp"

namespace {

void UIntToDecimalString(uint32_t value, char* out, int out_len) {
    if (out_len <= 0) {
        return;
    }
    if (value == 0) {
        if (out_len > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }
    char rev[16];
    int len = 0;
    while (value > 0 && len < static_cast<int>(sizeof(rev))) {
        rev[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    int w = 0;
    for (int i = len - 1; i >= 0 && w + 1 < out_len; --i) {
        out[w++] = rev[i];
    }
    out[w] = '\0';
}

void UInt64ToDecimalString(uint64_t value, char* out, int out_len) {
    if (out_len <= 0) {
        return;
    }
    if (value == 0) {
        if (out_len > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }
    char rev[32];
    int len = 0;
    while (value > 0 && len < static_cast<int>(sizeof(rev))) {
        rev[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    int w = 0;
    for (int i = len - 1; i >= 0 && w + 1 < out_len; --i) {
        out[w++] = rev[i];
    }
    out[w] = '\0';
}

}  // namespace

namespace ui {

SystemMonitorPanel::SystemMonitorPanel(Window* window, Layer* layer, LayerManager* layer_manager,
                                       int width, int height)
    : window_(window), layer_(layer), layer_manager_(layer_manager), width_(width), height_(height) {}

void SystemMonitorPanel::Refresh(uint64_t tick,
                                 uint64_t free_mib,
                                 uint32_t queue_count,
                                 uint64_t kbd_drop,
                                 uint64_t mouse_drop,
                                 bool jp_layout,
                                 bool ime_enabled) {
    const PixelColor kBg{14, 16, 22};
    const PixelColor kLabel{180, 188, 204};
    const PixelColor kValue{236, 238, 242};
    const PixelColor kTitle{220, 224, 232};
    const PixelColor kSubtle{160, 170, 190};

    if (!static_drawn_) {
        window_->FillRectangle(0, 0, width_, height_, kBg);
        window_->DrawString(12, 12, "System Monitor", kTitle);
        window_->DrawString(12, 34, "tick:", kLabel);
        window_->DrawString(12, 52, "free:", kLabel);
        window_->DrawString(152, 52, "MiB", kLabel);
        window_->DrawString(12, 70, "queue:", kLabel);
        window_->DrawString(12, 88, "kbd_drop:", kLabel);
        window_->DrawString(12, 106, "mouse_drop:", kLabel);
        window_->DrawString(12, 124, "layout:", kLabel);
        window_->DrawString(128, 124, "ime:", kLabel);
        window_->DrawString(12, 146, "drag/window input optimized", kSubtle);
        static_drawn_ = true;
        layer_manager_->Draw(layer_->GetX(), layer_->GetY(), width_, height_);
    }

    auto DrawValue = [&](int x, int y, int w, const char* text) {
        window_->FillRectangle(x, y, w, 16, kBg);
        window_->DrawString(x, y, text, kValue);
        layer_manager_->Draw(layer_->GetX() + x, layer_->GetY() + y, w, 16);
    };

    char num[40];
    UInt64ToDecimalString(tick, num, static_cast<int>(sizeof(num)));
    DrawValue(88, 34, 120, num);

    UInt64ToDecimalString(free_mib, num, static_cast<int>(sizeof(num)));
    DrawValue(88, 52, 60, num);

    UIntToDecimalString(queue_count, num, static_cast<int>(sizeof(num)));
    DrawValue(88, 70, 60, num);

    UInt64ToDecimalString(kbd_drop, num, static_cast<int>(sizeof(num)));
    DrawValue(88, 88, 120, num);

    UInt64ToDecimalString(mouse_drop, num, static_cast<int>(sizeof(num)));
    DrawValue(88, 106, 120, num);

    DrawValue(88, 124, 32, jp_layout ? "jp" : "us");
    DrawValue(168, 124, 32, ime_enabled ? "on" : "off");
}

}  // namespace ui

