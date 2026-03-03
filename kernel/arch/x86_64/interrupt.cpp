#include "interrupt.hpp"

// GDTの実体（NULL, Code[OS], Data[OS] の3つ）
SegmentDescriptor gdt[3];

// IDTの実体（割り込みは256種類あるので256個）
InterruptDescriptor idt[256];

void SetCodeSegment(SegmentDescriptor* desc) {
    desc->limit_low = 0;
    desc->base_low = 0;
    desc->base_middle = 0;
    desc->type = 10; // Execute/Read
    desc->system_segment = 1;
    desc->descriptor_privilege_level = 0;
    desc->present = 1;
    desc->limit_high = 0;
    desc->available = 0;
    desc->long_mode = 1; // 64-bit mode (Long Mode) flag
    desc->default_operation_size = 0;
    desc->granularity = 0;
    desc->base_high = 0;
}

void SetDataSegment(SegmentDescriptor* desc) {
    desc->limit_low = 0;
    desc->base_low = 0;
    desc->base_middle = 0;
    desc->type = 2; // Read/Write
    desc->system_segment = 1;
    desc->descriptor_privilege_level = 0;
    desc->present = 1;
    desc->limit_high = 0;
    desc->available = 0;
    desc->long_mode = 0;
    desc->default_operation_size = 1; // 32-bit (ignored usually in 64-bit mode)
    desc->granularity = 0;
    desc->base_high = 0;
}

void LoadGDT(uint16_t limit, uint64_t offset) {
    struct {
        uint16_t limit;
        uint64_t offset;
    } __attribute__((packed)) gdtr = {limit, offset};
    // lgdt命令でCPUにGDTの位置を登録
    __asm__ volatile("lgdt %0" : : "m"(gdtr));
}

void LoadIDT(uint16_t limit, uint64_t offset) {
    struct {
        uint16_t limit;
        uint64_t offset;
    } __attribute__((packed)) idtr = {limit, offset};
    // lidt命令でCPUにIDTの位置を登録
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void SetCS_DS(uint16_t cs, uint16_t ds) {
    // データ用セグメントレジスタの設定
    __asm__ volatile(
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        "mov %0, %%ss\n"
        : : "r"(ds)
    );
    // コード用(CS)の切り替えにはスタック(push/ret)を使った「擬似的なfar call/jmp」を用いる
    __asm__ volatile(
        "pushq %0\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : : "r"((uint64_t)cs) : "rax", "memory"
    );
}

void InitializeGDT() {
    // 0: Null Descriptor (未使用領域)
    *(uint64_t*)&gdt[0] = 0;
    
    // 1: OSカーネル用 64-bit コードセグメント
    SetCodeSegment(&gdt[1]);
    
    // 2: OSカーネル用 データセグメント
    SetDataSegment(&gdt[2]);

    // GDTを登録
    LoadGDT(sizeof(gdt) - 1, (uint64_t)&gdt[0]);
    
    // レジスタに「1番目のセグメントをコード領域(CS)に」「2番目のセグメントをデータ領域(DS等)に」指定 (1番目なので1 * 8 = 8, 2番目で16)
    SetCS_DS(1 * 8, 2 * 8);
}

void InitializeIDT() {
    // 全て0(無効な割り込み)として初期化だけ行う
    for (int i = 0; i < 256; ++i) {
        *(uint64_t*)&idt[i] = 0;
        *(((uint64_t*)&idt[i]) + 1) = 0;
    }
    
    // IDTを登録（これで少なくとも「テーブルはここにある」とCPUに教えた状態になる）
    LoadIDT(sizeof(idt) - 1, (uint64_t)&idt[0]);
}
