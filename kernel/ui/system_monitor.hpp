#pragma once

#include <stdint.h>

class Window;
class Layer;
class LayerManager;

namespace ui {

class SystemMonitorPanel {
public:
    SystemMonitorPanel(Window* window, Layer* layer, LayerManager* layer_manager,
                       int width, int height);
    void Refresh(uint64_t tick,
                 uint64_t free_mib,
                 uint32_t queue_count,
                 uint64_t kbd_drop,
                 uint64_t mouse_drop,
                 bool jp_layout,
                 bool ime_enabled);

private:
    Window* window_;
    Layer* layer_;
    LayerManager* layer_manager_;
    int width_;
    int height_;
    bool static_drawn_{false};
};

}  // namespace ui

