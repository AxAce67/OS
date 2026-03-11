#pragma once

#include <stdint.h>

class Window;
class Layer;
class LayerManager;

namespace ui {

class PointerTestPanel {
public:
    PointerTestPanel(Window* window, Layer* layer, LayerManager* layer_manager,
                     int width, int height);

    bool HandlePrimaryClick(int global_x, int global_y);

private:
    void DrawStatic();
    void DrawDynamic();

    Window* window_;
    Layer* layer_;
    LayerManager* layer_manager_;
    int width_;
    int height_;
    bool static_drawn_{false};
    bool toggled_{false};
    uint32_t click_count_{0};
};

}  // namespace ui
