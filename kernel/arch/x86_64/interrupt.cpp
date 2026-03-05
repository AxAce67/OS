#include "interrupt.hpp"

// GDTの実体（NULL, kernel code/data, user data/code, TSS(16-byte)）
SegmentDescriptor gdt[7];
TaskStateSegment64 tss64;

alignas(16) static uint8_t g_kernel_tss_stack[4096];

constexpr uint16_t kKernelCodeSelector = 1 * 8;
constexpr uint16_t kKernelDataSelector = 2 * 8;
constexpr uint16_t kTssSelector = 5 * 8;

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

void SetDataSegmentDPL(SegmentDescriptor* desc, uint8_t dpl) {
    SetDataSegment(desc);
    desc->descriptor_privilege_level = dpl & 0x3;
}

void SetCodeSegmentDPL(SegmentDescriptor* desc, uint8_t dpl) {
    SetCodeSegment(desc);
    desc->descriptor_privilege_level = dpl & 0x3;
}

void SetTSSDescriptor(SegmentDescriptor* low, SegmentDescriptor* high, uint64_t base, uint32_t limit) {
    low->limit_low = static_cast<uint16_t>(limit & 0xFFFF);
    low->base_low = static_cast<uint16_t>(base & 0xFFFF);
    low->base_middle = static_cast<uint8_t>((base >> 16) & 0xFF);
    low->type = 9;  // 64-bit Available TSS
    low->system_segment = 0;
    low->descriptor_privilege_level = 0;
    low->present = 1;
    low->limit_high = static_cast<uint8_t>((limit >> 16) & 0x0F);
    low->available = 0;
    low->long_mode = 0;
    low->default_operation_size = 0;
    low->granularity = 0;
    low->base_high = static_cast<uint8_t>((base >> 24) & 0xFF);

    uint64_t* high_raw = reinterpret_cast<uint64_t*>(high);
    *high_raw = (base >> 32) & 0xFFFFFFFFULL;
}

void LoadGDT(uint16_t limit, uint64_t offset) {
    struct {
        uint16_t limit;
        uint64_t offset;
    } __attribute__((packed)) gdtr = {limit, offset};
    // lgdt命令でCPUにGDTの位置を登録
    __asm__ volatile("lgdt %0" : : "m"(gdtr));
}

void LoadTR(uint16_t selector) {
    __asm__ volatile("ltr %0" : : "r"(selector));
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

    // 3: ユーザーデータセグメント (ring3)
    SetDataSegmentDPL(&gdt[3], 3);

    // 4: ユーザーコードセグメント (ring3)
    SetCodeSegmentDPL(&gdt[4], 3);

    for (int i = 0; i < static_cast<int>(sizeof(tss64)); ++i) {
        reinterpret_cast<uint8_t*>(&tss64)[i] = 0;
    }
    tss64.io_map_base = sizeof(TaskStateSegment64);
    tss64.rsp0 = reinterpret_cast<uint64_t>(&g_kernel_tss_stack[sizeof(g_kernel_tss_stack)]);
    SetTSSDescriptor(&gdt[5], &gdt[6], reinterpret_cast<uint64_t>(&tss64), sizeof(TaskStateSegment64) - 1);

    // GDTを登録
    LoadGDT(sizeof(gdt) - 1, (uint64_t)&gdt[0]);
    
    // レジスタに「1番目のセグメントをコード領域(CS)に」「2番目のセグメントをデータ領域(DS等)に」指定 (1番目なので1 * 8 = 8, 2番目で16)
    SetCS_DS(kKernelCodeSelector, kKernelDataSelector);
    LoadTR(kTssSelector);
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

void SetKernelTSSStack(uint64_t rsp0) {
    tss64.rsp0 = rsp0;
}

uint64_t GetKernelTSSStack() {
    return tss64.rsp0;
}

bool IsUserModeSegmentsReady() {
    return gdt[3].present == 1 &&
           gdt[4].present == 1 &&
           gdt[3].descriptor_privilege_level == 3 &&
           gdt[4].descriptor_privilege_level == 3 &&
           gdt[5].present == 1 &&
           gdt[5].system_segment == 0;
}
