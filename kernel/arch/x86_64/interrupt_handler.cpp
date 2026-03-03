#include "mouse.hpp"
#include "queue.hpp"
#include "interrupt.hpp"
#include "pic.hpp"
#include "io.hpp"
#include "console.hpp"
#include "apic.hpp"
#include "timer.hpp"
#include "ps2.hpp"
#include "input/message.hpp"

// kernel.cpp で定義しているコンソールとマウスカーソルの参照
extern Console* console;

// kernel.cpp で実体を定義するメインキュー
extern ArrayQueue<Message, 256>* main_queue;

volatile uint64_t g_keyboard_dropped_events = 0;
volatile uint64_t g_mouse_dropped_events = 0;

// 割り込みフレーム構造体
struct InterruptFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// PS/2マウスの状態管理用変数
static int mouse_phase = 0;
static int mouse_packet_bytes = 3;
static bool mouse_packet_initialized = false;
static uint8_t mouse_buf[4];

// PS/2マウスからの割り込みハンドラ
__attribute__((interrupt))
void IntHandlerMouse(InterruptFrame* frame) {
    (void)frame;
    // PICに「割り込み処理が終わりました」と報告する (IRQ12)
    SendEndOfInterrupt(12);

    // データポートから1バイト受信
    uint8_t data = In8(0x60);

    if (!mouse_packet_initialized) {
        mouse_packet_bytes = IsPS2MouseWheelEnabled() ? 4 : 3;
        mouse_packet_initialized = true;
    }

    // フェーズごとの受信処理（3バイトで1セットの移動情報になる）
    if (mouse_phase == 0) {
        // パケットの1バイト目は、ビット3が必ず1になる仕様（同期用）
        if ((data & 0x08) == 0) {
            return; // 同期ズレ、もしくはACKコマンドの返答なので無視
        }
    }

    mouse_buf[mouse_phase] = data;
    mouse_phase++;

    if (mouse_phase == mouse_packet_bytes) {
        // 3/4バイト揃った
        mouse_phase = 0; // 次のパケットのためにリセット

        int dx = mouse_buf[1];
        int dy = mouse_buf[2];
        int wheel = 0;

        // 1バイト目のビット情報から、dxとdyの符号（マイナスかどうか）を拡張する
        if ((mouse_buf[0] & 0x10) != 0) { dx |= 0xFFFFFF00; }
        if ((mouse_buf[0] & 0x20) != 0) { dy |= 0xFFFFFF00; }

        // PS/2マウスのY軸は上がプラスだが、画面座標は下がプラスなので反転する
        dy = -dy;

        if (mouse_packet_bytes == 4) {
            int z = mouse_buf[3] & 0x0F;
            if ((z & 0x08) != 0) {
                z |= 0xFFFFFFF0;
            }
            if (z > 0) {
                wheel = 1;
            } else if (z < 0) {
                wheel = -1;
            }
        }

        // **超重要：ここでは描画せず（VRAMアクセスは遅いため）、移動情報をキューに積んで一瞬で終わる**
        if (main_queue != nullptr) {
            Message msg;
            msg.type = Message::Type::kInterruptMouse;
            msg.pointer_mode = Message::PointerMode::kRelative;
            msg.dx = dx;
            msg.dy = dy;
            msg.x = 0;
            msg.y = 0;
            msg.wheel = wheel;
            msg.buttons = static_cast<uint8_t>(mouse_buf[0] & 0x07);
            msg.keycode = 0;
            if (!main_queue->Push(msg)) {
                ++g_mouse_dropped_events;
            }
        }
    }
}

__attribute__((interrupt))
void IntHandlerKeyboard(InterruptFrame* frame) {
    (void)frame;
    uint8_t data = In8(0x60);
    SendEndOfInterrupt(1);

    if (main_queue != nullptr) {
        Message msg;
        msg.type = Message::Type::kInterruptKeyboard;
        msg.pointer_mode = Message::PointerMode::kRelative;
        msg.dx = 0;
        msg.dy = 0;
        msg.x = 0;
        msg.y = 0;
        msg.wheel = 0;
        msg.buttons = 0;
        msg.keycode = data;
        if (!main_queue->Push(msg)) {
            ++g_keyboard_dropped_events;
        }
    }
}

__attribute__((interrupt))
void IntHandlerLAPICTimer(InterruptFrame* frame) {
    (void)frame;
    NotifyTimerTick();
    NotifyLocalAPICEOI();
}

// 割り込みハンドラの全レジスタ (IDT 256個のうち必要なものを埋める)
// 外部(interrupt.cpp)で定義した idt 変数を参照する
extern InterruptDescriptor idt[256];

void InitializeInterruptHandlers() {
    // キーボードの割り込みはIRQ1。PICのベース0x20 + 0x01(1) = 0x21
    SetInterruptDescriptor(&idt[0x21], (uint64_t)IntHandlerKeyboard,
                           1 * 8,
                           14,
                           0);

    // マウスの割り込みはIRQ12。PICのベース0x20 + 0x0C(12) = 0x2C
    SetInterruptDescriptor(&idt[0x2C], (uint64_t)IntHandlerMouse, 
                           1 * 8, // CS (Code Segment)はカーネルのものを指定
                           14,    // Interrupt Gate
                           0);    // DPL (Privilege Level)

    // LAPICローカルタイマ割り込みベクタ(0x40)
    SetInterruptDescriptor(&idt[0x40], (uint64_t)IntHandlerLAPICTimer,
                           1 * 8,
                           14,
                           0);
}

void EnqueueAbsolutePointerEvent(int32_t x, int32_t y, int32_t wheel, uint8_t buttons) {
    if (main_queue == nullptr) {
        return;
    }
    Message msg;
    msg.type = Message::Type::kInterruptMouse;
    msg.pointer_mode = Message::PointerMode::kAbsolute;
    msg.dx = 0;
    msg.dy = 0;
    msg.x = x;
    msg.y = y;
    msg.wheel = wheel;
    msg.buttons = buttons;
    msg.keycode = 0;
    if (!main_queue->Push(msg)) {
        ++g_mouse_dropped_events;
    }
}
