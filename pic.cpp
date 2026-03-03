#include "pic.hpp"
#include "io.hpp"

// PICのI/Oポート番号
const uint16_t kPIC0_ICW1 = 0x0020;
const uint16_t kPIC0_OCW2 = 0x0020;
const uint16_t kPIC0_IMR  = 0x0021;
const uint16_t kPIC0_ICW2 = 0x0021;
const uint16_t kPIC0_ICW3 = 0x0021;
const uint16_t kPIC0_ICW4 = 0x0021;

const uint16_t kPIC1_ICW1 = 0x00A0;
const uint16_t kPIC1_OCW2 = 0x00A0;
const uint16_t kPIC1_IMR  = 0x00A1;
const uint16_t kPIC1_ICW2 = 0x00A1;
const uint16_t kPIC1_ICW3 = 0x00A1;
const uint16_t kPIC1_ICW4 = 0x00A1;

void InitializePIC(uint8_t base_offset) {
    // 現在のマスクを保存（今回は全マスク(0xFF)状態からスタートするため不要だがセオリーとして）
    // uint8_t a1 = In8(kPIC0_IMR);
    // uint8_t a2 = In8(kPIC1_IMR);

    // ICW1: 初期化開始
    Out8(kPIC0_ICW1, 0x11); IoWait();
    Out8(kPIC1_ICW1, 0x11); IoWait();

    // ICW2: ベース割り込み番号の設定（マスター=0x20, スレーブ=0x28)
    Out8(kPIC0_ICW2, base_offset); IoWait();
    Out8(kPIC1_ICW2, base_offset + 8); IoWait();

    // ICW3: マスター/スレーブの接続設定
    Out8(kPIC0_ICW3, 4); IoWait(); // スレーブはIRQ2(ビット2)に接続されている
    Out8(kPIC1_ICW3, 2); IoWait(); // 自分の身分はIRQ2であることを通知

    // ICW4: 8086xモードを有効化
    Out8(kPIC0_ICW4, 0x01); IoWait();
    Out8(kPIC1_ICW4, 0x01); IoWait();

    // 全ての割り込みを一旦マスク(無効化)しておく
    Out8(kPIC0_IMR, 0xFF);
    Out8(kPIC1_IMR, 0xFF);
}

void UnmaskIRQ(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = kPIC0_IMR;
    } else {
        port = kPIC1_IMR;
        irq -= 8;
    }
    
    // 現在のマスク値を読み取り、該当するビットだけを0(許可)にする
    value = In8(port) & ~(1 << irq);
    Out8(port, value);
}

void SendEndOfInterrupt(uint8_t irq) {
    if (irq >= 8) {
        Out8(kPIC1_OCW2, 0x20); // スレーブPICに終了通知
    }
    Out8(kPIC0_OCW2, 0x20);     // マスターPICに終了通知
}
