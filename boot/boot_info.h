// boot_info.h
// ブートローダー(main.c)からカーネルへ、ハードウェア情報を一括して渡すための構造体

#pragma once

#include <stdint.h>
#include "frame_buffer_config.h"

// UEFIのEFI_MEMORY_DESCRIPTORと互換の構造体定義
struct MemoryDescriptor {
    uint32_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

// UEFIにおけるOS使用可能メモリのタイプ番号 (EfiConventionalMemory)
const uint32_t kEfiConventionalMemory = 7;

struct BootInfo {
    struct FrameBufferConfig* frame_buffer_config;
    
    // メモリ領域の情報
    uint8_t* memory_map;      // MemoryDescriptorの配列の先頭ポインタ
    uint64_t memory_map_size; // 配列全体のバイトサイズ
    uint64_t descriptor_size; // 配列の要素1つぶんのバイトサイズ
};
