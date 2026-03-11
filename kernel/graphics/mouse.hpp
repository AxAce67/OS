#pragma once
#include <stdint.h>
#include "layer.hpp"

// マウスカーソルのドット絵のサイズ
const int kMouseCursorWidth = 15;
const int kMouseCursorHeight = 24;

class Window;

class MouseCursor {
public:
    MouseCursor(unsigned int initial_x, unsigned int initial_y, LayerManager* layer_manager);
    void Move(int dx, int dy);
    void SetPosition(int x, int y);
    void Redraw();
    int X() const;
    int Y() const;

private:
    void DrawCursorWindow();
    LayerManager* layer_manager_;
    Layer* layer_{nullptr};
    Window* window_{nullptr};
    int x_{0};
    int y_{0};
};
