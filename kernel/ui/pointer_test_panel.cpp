#include "ui/pointer_test_panel.hpp"

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

constexpr int kButtonX = 14;
constexpr int kButtonY = 46;
constexpr int kButtonW = 132;
constexpr int kButtonH = 28;

}  // namespace

namespace ui {

PointerTestPanel::PointerTestPanel(Window* window, Layer* layer, LayerManager* layer_manager,
                                   int width, int height)
    : window_(window), layer_(layer), layer_manager_(layer_manager), width_(width), height_(height) {
    DrawStatic();
    DrawDynamic();
}

void PointerTestPanel::DrawStatic() {
    const PixelColor kFrame{78, 82, 94};
    const PixelColor kBody{18, 20, 28};
    const PixelColor kTitle{228, 232, 238};
    const PixelColor kText{180, 188, 204};

    window_->FillRectangle(0, 0, width_, height_, kFrame);
    window_->FillRectangle(2, 2, width_ - 4, height_ - 4, kBody);
    window_->DrawString(12, 10, "Pointer Test", kTitle);
    window_->DrawString(12, 28, "Left click the button", kText);
    static_drawn_ = true;
    layer_manager_->Draw(layer_->GetX(), layer_->GetY(), width_, height_);
}

void PointerTestPanel::DrawDynamic() {
    const PixelColor kBody{18, 20, 28};
    const PixelColor kText{236, 238, 242};
    const PixelColor kSubtle{170, 178, 194};
    const PixelColor kOn{58, 150, 100};
    const PixelColor kOff{150, 72, 72};

    window_->FillRectangle(kButtonX, kButtonY, kButtonW, kButtonH, toggled_ ? kOn : kOff);
    window_->DrawString(kButtonX + 18, kButtonY + 8, toggled_ ? "ON" : "OFF", kText);

    window_->FillRectangle(14, 84, width_ - 28, 20, kBody);
    window_->DrawString(14, 84, "clicks:", kSubtle);
    char count[16];
    UIntToDecimalString(click_count_, count, static_cast<int>(sizeof(count)));
    window_->DrawString(72, 84, count, kText);

    layer_manager_->Draw(layer_->GetX(), layer_->GetY(), width_, height_);
}

bool PointerTestPanel::HandlePrimaryClick(int global_x, int global_y) {
    const int local_x = global_x - layer_->GetX();
    const int local_y = global_y - layer_->GetY();
    if (local_x < kButtonX || local_y < kButtonY ||
        local_x >= kButtonX + kButtonW || local_y >= kButtonY + kButtonH) {
        return false;
    }
    toggled_ = !toggled_;
    ++click_count_;
    if (!static_drawn_) {
        DrawStatic();
    }
    DrawDynamic();
    return true;
}

}  // namespace ui
