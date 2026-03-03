// frame_buffer_config.h
// ブートローダーからカーネルへ渡す画面（フレームバッファ）の情報

#pragma once
#include <stdint.h>

// ピクセルの並び順のフォーマット定義
typedef enum {
    kPixelRGBResv8BitPerColor,
    kPixelBGRResv8BitPerColor,
} PixelFormat;

// カーネルに渡すフレームバッファ情報の構造体
struct FrameBufferConfig {
    uint8_t* frame_buffer;         // フレームバッファ（ピクセル用メモリ）の先頭アドレス
    uint32_t pixels_per_scan_line; // 1行あたりのピクセル数（横幅やパディング込みの数）
    uint32_t horizontal_resolution;// 横の解像度
    uint32_t vertical_resolution;  // 縦の解像度
    PixelFormat pixel_format;      // RGBかBGRか等のフォーマット
};
