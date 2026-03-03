#pragma once
#include <stdint.h>

// 割り込みコントローラ(PIC)を初期化し、割り込みを受け付ける状態にする
// base_offset: マッピングする割り込み番号のベース（通常は0x20）
void InitializePIC(uint8_t base_offset);

// 特定の割り込み(IRQ)のマスクを解除する（許可する）
void UnmaskIRQ(uint8_t irq);

// 割り込み処理が完了したことをPICに通知する (End Of Interrupt)
void SendEndOfInterrupt(uint8_t irq);
