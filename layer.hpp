// layer.hpp
#pragma once
#include "window.hpp"

// ピクセル描画先となるVRAMの情報を持つ構造体 (boot_info.hなどと共通)
struct FrameBufferConfig;

// 1つの画面要素（ウィンドウなどの画像データ）とその配置座標を持つクラス
class Layer {
public:
    Layer(unsigned int id) : id_(id), window_(nullptr), x_(0), y_(0) {}

    // IDの取得
    unsigned int ID() const { return id_; }

    // レイヤーに実際の画像データ(Window)を割り当てる
    Layer& SetWindow(Window* window) {
        window_ = window;
        return *this;
    }

    // 画面上の表示位置を設定する
    Layer& Move(int x, int y) {
        x_ = x;
        y_ = y;
        return *this;
    }

    // Z移動（LayerManagerが使用する）
    Layer& MoveRelative(int dx, int dy) {
        x_ += dx;
        y_ += dy;
        return *this;
    }

    Window* GetWindow() const { return window_; }
    int GetX() const { return x_; }
    int GetY() const { return y_; }

    // VRAM等のフレームバッファに、自身のウィンドウ内容を全体描画する
    void DrawTo(const FrameBufferConfig& config) const;

    // 指定された矩形領域（area_x, area_y, area_w, area_h）と自身が交差する部分だけを再描画する最適化版
    void DrawTo(const FrameBufferConfig& config, int area_x, int area_y, int area_w, int area_h) const;

private:
    unsigned int id_;
    Window* window_;
    int x_, y_; // 画面上の描画開始位置
};

// 複数のレイヤーを管理し、下から順番に描画(コンポジット)するクラス
class LayerManager {
public:
    LayerManager(const FrameBufferConfig& config);

    // 新しいレイヤーを作成してIDを返す
    Layer* NewLayer();

    // 指定したIDのレイヤーのZオーダーを変更する（背面=0、手前=値大）
    // hideしたければ負の値を指定するなどのロジックも可能(今回は簡易実装)
    void UpDown(Layer* layer, int new_height);

    // 見えている全レイヤーを重なり順にVRAMへ描画する
    void Draw() const;
    
    // 特定の矩形領域(x, y, w, h)だけを再描画する最適化版Draw
    void Draw(int x, int y, int width, int height) const;

    // 画面解像度などを参照するためのゲッター
    const FrameBufferConfig& GetConfig() const { return config_; }

private:
    const FrameBufferConfig& config_;
    
    // 動的確保されたレイヤーのリスト (単純化のため生ポインタ配列と現在数で管理)
    // std::vectorがまだ無いため、固定長配列(最大256個)で管理する
    Layer* layers_[256];
    int latest_id_{1};

    // 実際に画面に表示される順序（Zオーダー: 0が最背面）
    Layer* layer_stack_[256];
    int height_{0}; // 現在スタックに積まれている数
};
