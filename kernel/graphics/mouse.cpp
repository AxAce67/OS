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
    // 古い座標を記憶
    int old_x = layer_->GetX();
    int old_y = layer_->GetY();
    
    // 一度相対移動させる
    layer_->MoveRelative(dx, dy);

    // 新しい座標が画面外（上下左右）に出てしまった場合は画面端に押し戻す（クランプ処理）
    // （完全に見失うことや、クリッピング計算のオーバーフローを防ぐため）
    int new_x = layer_->GetX();
    int new_y = layer_->GetY();
    int w = layer_->GetWindow()->Width();
    int h = layer_->GetWindow()->Height();

    // LayerManagerから実際の画面解像度を取得してクランプする
    const auto& config = layer_manager_->GetConfig();
    int res_x = static_cast<int>(config.horizontal_resolution);
    int res_y = static_cast<int>(config.vertical_resolution);

    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x >= res_x) new_x = res_x - 1; 
    if (new_y >= res_y) new_y = res_y - 1;

    // 押し戻した結果を再設定
    layer_->Move(new_x, new_y);

    // 最終的な描画更新用座標
    int final_x = layer_->GetX();
    int final_y = layer_->GetY();

    // 古い位置（消去用）と新しい位置（描画用）だけを部分的にコンポジット再描画する
    layer_manager_->Draw(old_x, old_y, w, h);
    layer_manager_->Draw(final_x, final_y, w, h);
}
