#include "ps2.hpp"
#include "io.hpp"

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

    // 3. マウス本体に「データ送信開始してね」と伝える
    WaitKBC_SendReady();
    Out8(0x64, 0xD4); // 次のデータを第2ポート(マウス)へ送るコマンド
    WaitKBC_SendReady();
    Out8(0x60, 0xF4); // データ送信開始(Enable Packet Streaming)コマンド
    
    // マウスから「了解(0xFA=ACK)」という返事が来るのを待って読み捨てる (確実な動作のため)
    WaitKBC_ReceiveReady();
    In8(0x60);
}
