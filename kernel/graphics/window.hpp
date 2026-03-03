// window.hpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// RGBAなどのピクセル色情報を表す構造体
struct PixelColor {
    uint8_t r, g, b;
};

// 1つのウィンドウ（独立した画像キャンバス）を表すクラス
class Window {
public:
    Window(int width, int height);
    ~Window(); // ヒープの解放処理

    // コピーとムーブの禁止 (単純化のため)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    int Width() const { return width_; }
    int Height() const { return height_; }
    const PixelColor* Buffer() const { return data_; }

    // 指定座標にピクセル色を書き込む
    void DrawPixel(int x, int y, const PixelColor& c);
    
    // 指定した矩形範囲を指定した色で塗りつぶす
    void FillRectangle(int x, int y, int width, int height, const PixelColor& c);

    // 文字列の描画（フォントデータを利用）
    void DrawChar(int x, int y, char c, const PixelColor& color);
    void DrawCharScaled(int x, int y, char c, const PixelColor& color, int scale);
    void DrawString(int x, int y, const char* s, const PixelColor& color);

    void SetTransparentColor(const PixelColor& c);
    void ClearTransparentColor();
    bool HasTransparentColor() const;
    const PixelColor& TransparentColor() const;

private:
    int width_, height_;
    PixelColor* data_; // [width * height] のピクセル配列へのポインタ
    bool has_transparent_color_{false};
    PixelColor transparent_color_{0, 0, 0};
};
