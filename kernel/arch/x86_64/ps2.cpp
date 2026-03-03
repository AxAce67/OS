#include "ps2.hpp"
#include "io.hpp"

static bool g_ps2_mouse_wheel_enabled = false;

// PS/2コントローラのステータス確認用
void WaitKBC_SendReady() {
    // 0x64ポートのビット1が0になるまで待つ
    while ((In8(0x64) & 0x02) != 0) {
        // wait
    }
}

void WaitKBC_ReceiveReady() {
    // 0x64ポートのビット0が1になるまで待つ
    while ((In8(0x64) & 0x01) == 0) {
        // wait
    }
}

void SendMouseCommand(uint8_t cmd) {
    WaitKBC_SendReady();
    Out8(0x64, 0xD4);
    WaitKBC_SendReady();
    Out8(0x60, cmd);
    WaitKBC_ReceiveReady();
    In8(0x60); // ACK(0xFA) を読み捨て
}

void SetMouseSampleRate(uint8_t rate) {
    SendMouseCommand(0xF3);
    SendMouseCommand(rate);
}

uint8_t GetMouseDeviceId() {
    WaitKBC_SendReady();
    Out8(0x64, 0xD4);
    WaitKBC_SendReady();
    Out8(0x60, 0xF2);
    WaitKBC_ReceiveReady();
    In8(0x60); // ACK
    WaitKBC_ReceiveReady();
    return In8(0x60); // Device ID
}

bool IsPS2MouseWheelEnabled() {
    return g_ps2_mouse_wheel_enabled;
}

void InitializePS2Mouse() {
    // 1. マウス(第2PS/2ポート)を有効化
    WaitKBC_SendReady();
    Out8(0x64, 0xA8); // コマンド: セカンドPS/2ポート有効化

    // 2. マウスの割り込み(IRQ12)をオンにする設定
    WaitKBC_SendReady();
    Out8(0x64, 0x20); // コマンド: 読み取り （コンフィグバイト）
    WaitKBC_ReceiveReady();
    uint8_t config = In8(0x60); // 設定値を取得

    WaitKBC_SendReady();
    Out8(0x64, 0x60); // コマンド: 書き込み （コンフィグバイト）
    WaitKBC_SendReady();
    Out8(0x60, config | 0x03); // IRQ1/IRQ12（第1/第2ポート）を有効化して書き戻す

    // 3. IntelliMouse シーケンスでホイール機能を有効化
    SetMouseSampleRate(200);
    SetMouseSampleRate(100);
    SetMouseSampleRate(80);
    g_ps2_mouse_wheel_enabled = (GetMouseDeviceId() == 3);

    // 4. マウス本体に「データ送信開始してね」と伝える
    SendMouseCommand(0xF4); // Enable Packet Streaming
}
