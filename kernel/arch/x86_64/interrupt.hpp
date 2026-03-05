#pragma once
#include <stdint.h>

// セグメントディスクリプタ（GDTの1要素）: 8バイト
struct SegmentDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  type : 4;
    uint8_t  system_segment : 1;
    uint8_t  descriptor_privilege_level : 2;
    uint8_t  present : 1;
    uint8_t  limit_high : 4;
    uint8_t  available : 1;
    uint8_t  long_mode : 1;
    uint8_t  default_operation_size : 1;
    uint8_t  granularity : 1;
    uint8_t  base_high;
} __attribute__((packed));

void SetCodeSegment(SegmentDescriptor* desc);
void SetDataSegment(SegmentDescriptor* desc);

// 割り込みディスクリプタ（IDTの1要素）: 16バイト (x86_64)
struct InterruptDescriptor {
    uint16_t offset_low;
    uint16_t segment_selector;
    uint16_t attr; // IST(0~2), Type(8~11), DPL(13~14), Present(15)
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved2;
} __attribute__((packed));

struct TaskStateSegment64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

inline void SetInterruptDescriptor(InterruptDescriptor* desc, uint64_t offset, uint16_t segment_selector, uint8_t type, uint8_t dpl) {
    desc->offset_low = offset & 0xFFFF;
    desc->segment_selector = segment_selector;
    desc->attr = (type << 8) | (dpl << 13) | (1 << 15);
    desc->offset_middle = (offset >> 16) & 0xFFFF;
    desc->offset_high = offset >> 32;
    desc->reserved2 = 0;
}

void InitializeGDT();
void InitializeIDT();
void LoadIDT(uint16_t limit, uint64_t offset);
void SetKernelTSSStack(uint64_t rsp0);
uint64_t GetKernelTSSStack();
bool IsUserModeSegmentsReady();
