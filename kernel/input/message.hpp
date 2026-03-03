#pragma once
#include <stdint.h>

// 割り込みハンドラとメインループ間で共有するイベントメッセージ
struct Message {
    enum class Type : uint8_t {
        kInterruptMouse,
        kInterruptKeyboard,
    } type;

    // ポインタイベントの座標形式
    enum class PointerMode : uint8_t {
        kRelative,
        kAbsolute,
    } pointer_mode;

    int32_t dx, dy;   // relative move
    int32_t x, y;     // absolute position
    int32_t wheel;
    uint8_t buttons;
    uint8_t keycode;
};
