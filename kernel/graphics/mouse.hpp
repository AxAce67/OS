#pragma once
#include <stdint.h>
#include "frame_buffer_config.h"

#include "layer.hpp"

// マウスカーソルのドット絵のサイズ
const int kMouseCursorWidth = 15;
const int kMouseCursorHeight = 24;

class MouseCursor {
public:
    MouseCursor(unsigned int initial_x, unsigned int initial_y, LayerManager* layer_manager);
    void Move(int dx, int dy);
    void SetPosition(int x, int y);
    int X() const;
    int Y() const;

private:
    void DrawCursorAt(int x, int y);
    LayerManager* layer_manager_;
    int x_{0};
    int y_{0};
};
