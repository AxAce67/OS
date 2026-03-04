// layer.cpp
#include "layer.hpp"
#include "boot_info.h" // FrameBufferConfig

// DrawTo: 自身のWindowクラスが持つピクセル配列から、実際の画面(FrameBufferConfig)へと色を転送する
void Layer::DrawTo(const FrameBufferConfig& config) const {
    if (!window_) return;
    DrawTo(config, 0, 0, config.horizontal_resolution, config.vertical_resolution);
}

// 矩形最適化版のDrawTo
void Layer::DrawTo(const FrameBufferConfig& config, int area_x, int area_y, int area_w, int area_h) const {
    if (!window_) return;

    uint8_t* vram = config.frame_buffer;
    
    // 自身のレイヤーが持つウィンドウサイズと、再描画エリア(area)が被る範囲を計算
    int win_start_x = x_;
    int win_start_y = y_;
    int win_end_x = x_ + window_->Width();
    int win_end_y = y_ + window_->Height();

    int area_end_x = area_x + area_w;
    int area_end_y = area_y + area_h;

    // 交差する範囲（VRAM上での描画対象の開始〜終了座標）を求める
    int draw_start_x = (win_start_x > area_x) ? win_start_x : area_x;
    int draw_start_y = (win_start_y > area_y) ? win_start_y : area_y;
    int draw_end_x   = (win_end_x < area_end_x) ? win_end_x : area_end_x;
    int draw_end_y   = (win_end_y < area_end_y) ? win_end_y : area_end_y;

    // もし描画対象エリアと被っていなければ何もしない
    if (draw_start_x >= draw_end_x || draw_start_y >= draw_end_y) {
        return; 
    }

    // 最後に画面（VRAM）の物理的な解像度(0〜resolution)でクリッピング
    int res_x = static_cast<int>(config.horizontal_resolution);
    int res_y = static_cast<int>(config.vertical_resolution);

    if (draw_start_x < 0) draw_start_x = 0;
    if (draw_start_y < 0) draw_start_y = 0;
    if (draw_end_x > res_x) draw_end_x = res_x;
    if (draw_end_y > res_y) draw_end_y = res_y;

    if (draw_start_x >= draw_end_x || draw_start_y >= draw_end_y) {
        return; // 描画対象エリアと被っていない
    }

    for (int vram_y = draw_start_y; vram_y < draw_end_y; ++vram_y) {
        // 画面外のクリッピング（負の座標から入ってくる可能性を考慮してintで比較）
        if (vram_y < 0 || vram_y >= static_cast<int>(config.vertical_resolution)) continue;

        for (int vram_x = draw_start_x; vram_x < draw_end_x; ++vram_x) {
            if (vram_x < 0 || vram_x >= static_cast<int>(config.horizontal_resolution)) continue;

            // VRAM座標(vram_x, vram_y)を、Window内のローカル座標(win_x, win_y)に戻す
            int win_x = vram_x - x_;
            int win_y = vram_y - y_;

            const PixelColor* p = window_->Buffer();
            PixelColor c = p[win_y * window_->Width() + win_x];

            if (window_->HasTransparentColor()) {
                const PixelColor& tc = window_->TransparentColor();
                if (c.r == tc.r && c.g == tc.g && c.b == tc.b) {
                    continue; // 透過
                }
            }

            uint32_t index = (vram_y * config.pixels_per_scan_line + vram_x) * 4;
            if (config.pixel_format == kPixelRGBResv8BitPerColor) {
                vram[index]     = c.r; // Red
                vram[index + 1] = c.g; // Green
                vram[index + 2] = c.b; // Blue
            } else {
                vram[index]     = c.b; // Blue
                vram[index + 1] = c.g; // Green
                vram[index + 2] = c.r; // Red
            }
        }
    }
}

// LayerManagerのコンストラクタ
LayerManager::LayerManager(const FrameBufferConfig& config)
    : config_(config),
      back_buffer_width_(static_cast<int>(config.horizontal_resolution)),
      back_buffer_height_(static_cast<int>(config.vertical_resolution)) {
    // ポインタ配列をゼロクリア
    for (int i = 0; i < 256; ++i) {
        layers_[i] = nullptr;
        layer_stack_[i] = nullptr;
    }
    const uint64_t pixels = static_cast<uint64_t>(back_buffer_width_) * static_cast<uint64_t>(back_buffer_height_);
    if (pixels > 0) {
        back_buffer_ = new PixelColor[pixels];
    }
}

LayerManager::~LayerManager() {
    delete[] back_buffer_;
}

// 新規のレイヤー（Windowの置き場）を作成して割り当てる
Layer* LayerManager::NewLayer() {
    if (latest_id_ >= 256) {
        return nullptr; // とりあえず上限オーバー時は作れない
    }

    Layer* layer = new Layer(latest_id_);
    layers_[latest_id_] = layer;
    latest_id_++;
    return layer;
}

// Zオーダーを変更する
// （今回は簡易版として、指定された高さ「new_height」に挿入し、それ以降をずらして再構築する）
void LayerManager::UpDown(Layer* layer, int new_height) {
    // マイナス値なら「消す」扱いとする（描画スタックから降ろす）
    if (new_height < 0) {
        // スタックの中から対象レイヤーを探し、後ろを詰める
        int found = -1;
        for (int i = 0; i < height_; ++i) {
            if (layer_stack_[i] == layer) {
                found = i;
                break;
            }
        }
        if (found != -1) {
            for (int i = found; i < height_ - 1; ++i) {
                layer_stack_[i] = layer_stack_[i + 1];
            }
            height_--;
        }
        return;
    }

    // 既にスタック上にある場合は、一度取り除いてから再挿入する仕組みが必要（簡易的に一度消す処理を入れる）
    UpDown(layer, -1);
    
    // new_heightが現在のheight_より大きければ一番上に置く
    if (new_height > height_) {
        new_height = height_;
    }
    
    // 挿入位置から後ろを1つずつ後ろにずらす
    for (int i = height_; i > new_height; --i) {
        layer_stack_[i] = layer_stack_[i - 1];
    }
    layer_stack_[new_height] = layer;
    height_++;
}

// 登録されている全レイヤーを下(0)から順に画面(VRAM)へ描き込む
void LayerManager::Draw() const {
    Draw(0, 0, static_cast<int>(config_.horizontal_resolution), static_cast<int>(config_.vertical_resolution));
}

// 全体を再描画するのではなく、指定された矩形（x, y, w, h）の中身だけを描画し直す最適化メソッド。
// マウスの移動時など「狭い範囲」だけを描画更新するときに使う。
void LayerManager::Draw(int x, int y, int width, int height) const {
    if (back_buffer_ == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const int res_x = static_cast<int>(config_.horizontal_resolution);
    const int res_y = static_cast<int>(config_.vertical_resolution);
    int draw_start_x = x;
    int draw_start_y = y;
    int draw_end_x = x + width;
    int draw_end_y = y + height;
    if (draw_start_x < 0) draw_start_x = 0;
    if (draw_start_y < 0) draw_start_y = 0;
    if (draw_end_x > res_x) draw_end_x = res_x;
    if (draw_end_y > res_y) draw_end_y = res_y;
    if (draw_start_x >= draw_end_x || draw_start_y >= draw_end_y) {
        return;
    }

    // 1) 完全に覆う不透明レイヤーを見つけられる場合は、それを合成の基準にして下層を省く。
    int base_layer_index = -1;
    for (int i = height_ - 1; i >= 0; --i) {
        Layer* layer = layer_stack_[i];
        if (layer == nullptr || layer->GetWindow() == nullptr) {
            continue;
        }
        Window* win = layer->GetWindow();
        if (win->HasTransparentColor()) {
            continue;
        }
        const int win_start_x = layer->GetX();
        const int win_start_y = layer->GetY();
        const int win_end_x = win_start_x + win->Width();
        const int win_end_y = win_start_y + win->Height();
        if (win_start_x <= draw_start_x && win_start_y <= draw_start_y &&
            win_end_x >= draw_end_x && win_end_y >= draw_end_y) {
            base_layer_index = i;
            break;
        }
    }

    if (base_layer_index >= 0) {
        Layer* base = layer_stack_[base_layer_index];
        Window* win = base->GetWindow();
        const PixelColor* src = win->Buffer();
        const int win_w = win->Width();
        const int sx = draw_start_x - base->GetX();
        for (int py = draw_start_y; py < draw_end_y; ++py) {
            const int sy = py - base->GetY();
            const PixelColor* src_row = &src[sy * win_w + sx];
            PixelColor* dst_row = &back_buffer_[py * back_buffer_width_ + draw_start_x];
            const int span_w = draw_end_x - draw_start_x;
            for (int j = 0; j < span_w; ++j) {
                dst_row[j] = src_row[j];
            }
        }
    } else {
        // 基準レイヤーがない場合のみクリアして合成。
        const int clear_width = draw_end_x - draw_start_x;
        for (int py = draw_start_y; py < draw_end_y; ++py) {
            PixelColor* dst = &back_buffer_[py * back_buffer_width_ + draw_start_x];
            for (int j = 0; j < clear_width; ++j) {
                dst[j] = PixelColor{0, 0, 0};
            }
        }
    }

    // 2) 対象矩形へレイヤーを基準位置より上から順に合成。
    const int compose_begin = (base_layer_index >= 0) ? (base_layer_index + 1) : 0;
    for (int i = compose_begin; i < height_; ++i) {
        Layer* layer = layer_stack_[i];
        if (layer == nullptr || layer->GetWindow() == nullptr) {
            continue;
        }
        Window* win = layer->GetWindow();
        const int win_start_x = layer->GetX();
        const int win_start_y = layer->GetY();
        const int win_end_x = win_start_x + win->Width();
        const int win_end_y = win_start_y + win->Height();

        int lx0 = (win_start_x > draw_start_x) ? win_start_x : draw_start_x;
        int ly0 = (win_start_y > draw_start_y) ? win_start_y : draw_start_y;
        int lx1 = (win_end_x < draw_end_x) ? win_end_x : draw_end_x;
        int ly1 = (win_end_y < draw_end_y) ? win_end_y : draw_end_y;
        if (lx0 >= lx1 || ly0 >= ly1) {
            continue;
        }

        const PixelColor* src = win->Buffer();
        const bool has_tc = win->HasTransparentColor();
        const PixelColor tc = win->TransparentColor();
        const int win_w = win->Width();
        if (!has_tc) {
            const int span_w = lx1 - lx0;
            for (int py = ly0; py < ly1; ++py) {
                const int wy = py - win_start_y;
                const PixelColor* src_row = &src[wy * win_w + (lx0 - win_start_x)];
                PixelColor* dst_row = &back_buffer_[py * back_buffer_width_ + lx0];
                for (int i = 0; i < span_w; ++i) {
                    dst_row[i] = src_row[i];
                }
            }
        } else {
            for (int py = ly0; py < ly1; ++py) {
                const int wy = py - win_start_y;
                for (int px = lx0; px < lx1; ++px) {
                    const int wx = px - win_start_x;
                    const PixelColor c = src[wy * win_w + wx];
                    if (c.r == tc.r && c.g == tc.g && c.b == tc.b) {
                        continue;
                    }
                    back_buffer_[py * back_buffer_width_ + px] = c;
                }
            }
        }
    }

    // 3) 合成済みバックバッファをVRAMへ一括転送。
    const uint32_t pitch = config_.pixels_per_scan_line;
    if (config_.pixel_format == kPixelRGBResv8BitPerColor) {
        for (int py = draw_start_y; py < draw_end_y; ++py) {
            uint8_t* vram_row = &config_.frame_buffer[(static_cast<uint32_t>(py) * pitch +
                                                       static_cast<uint32_t>(draw_start_x)) * 4];
            const PixelColor* src_row = &back_buffer_[py * back_buffer_width_ + draw_start_x];
            const int span_w = draw_end_x - draw_start_x;
            for (int i = 0; i < span_w; ++i) {
                const PixelColor c = src_row[i];
                vram_row[0] = c.r;
                vram_row[1] = c.g;
                vram_row[2] = c.b;
                vram_row += 4;
            }
        }
    } else {
        for (int py = draw_start_y; py < draw_end_y; ++py) {
            uint8_t* vram_row = &config_.frame_buffer[(static_cast<uint32_t>(py) * pitch +
                                                       static_cast<uint32_t>(draw_start_x)) * 4];
            const PixelColor* src_row = &back_buffer_[py * back_buffer_width_ + draw_start_x];
            const int span_w = draw_end_x - draw_start_x;
            for (int i = 0; i < span_w; ++i) {
                const PixelColor c = src_row[i];
                vram_row[0] = c.b;
                vram_row[1] = c.g;
                vram_row[2] = c.r;
                vram_row += 4;
            }
        }
    }
    ++draw_generation_;
}
