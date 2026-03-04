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

MouseCursor::MouseCursor(unsigned int initial_x, unsigned int initial_y, LayerManager* layer_manager)
    : layer_manager_(layer_manager) {
    
    // 1. マウスカーソル用の15x24ピクセルの描画キャンバス(Window)を作る
    Window* window = new Window(kMouseCursorWidth, kMouseCursorHeight);
    window->SetTransparentColor({0, 0, 0}); // 黒色を透過色に指定
    
    // 背景(ゴミメモリ)をすべて透過色(0,0,0)で初期化しておく
    window->FillRectangle(0, 0, kMouseCursorWidth, kMouseCursorHeight, {0, 0, 0});

    // 2. そこにドット絵を書き込む（' 'は黒で塗っておく。layer.cppが黒(0,0,0)を透明色としてスキップする仕様）
    for (int y = 0; y < kMouseCursorHeight; ++y) {
        for (int x = 0; x < kMouseCursorWidth; ++x) {
            char c = mouse_cursor_shape[y][x];
            if (c == '@') {
                window->DrawPixel(x, y, {0, 0, 0});       // フチ
            } else if (c == '.') {
                window->DrawPixel(x, y, {255, 255, 255}); // 白
            } else {
                window->DrawPixel(x, y, {0, 0, 0});       // 透明（layer側でスキップされる）
            }
            // ※「フチの黒」と「透明の黒」が被ってしまう問題がありますが簡易実装としてまずは統合します。
            // 　（フチを少しグレー {1,1,1} などにして透過の黒 {0,0,0} と区別する小技を使います）
            if (c == '@') {
                window->DrawPixel(x, y, {1, 1, 1}); // 限りなく黒に近いグレーをフチにする
            }
        }
    }

    // 3. LayerManager に登録し、位置とウィンドウを設定
    layer_ = layer_manager_->NewLayer();
    layer_->SetWindow(window)
          .Move(initial_x, initial_y);
          
    // マウスカーソルなので Zオーダー は一番上に設定する (例: 100)
    layer_manager_->UpDown(layer_, 100);
}

void MouseCursor::Move(int dx, int dy) {
    SetPosition(layer_->GetX() + dx, layer_->GetY() + dy);
}

void MouseCursor::SetPosition(int x, int y) {
    int old_x = layer_->GetX();
    int old_y = layer_->GetY();
    int w = layer_->GetWindow()->Width();
    int h = layer_->GetWindow()->Height();

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

    layer_->Move(x, y);

    const int final_x = layer_->GetX();
    const int final_y = layer_->GetY();
    const int x0 = (old_x < final_x) ? old_x : final_x;
    const int y0 = (old_y < final_y) ? old_y : final_y;
    const int x1 = ((old_x + w) > (final_x + w)) ? (old_x + w) : (final_x + w);
    const int y1 = ((old_y + h) > (final_y + h)) ? (old_y + h) : (final_y + h);
    layer_manager_->Draw(x0, y0, x1 - x0, y1 - y0);
}

int MouseCursor::X() const {
    return layer_->GetX();
}

int MouseCursor::Y() const {
    return layer_->GetY();
}
