// memory.cpp
#include "memory.hpp"
#include "console.hpp"

extern Console* console;

MemoryManager* memory_manager;

MemoryManager::MemoryManager() {
    // 全てのメモリ領域をまず「使用中(1)」で埋めておく
    // その後で、安全に使える空きエリアだけを0にリセットしていく
    for (size_t i = 0; i < (kMaxPages / 64); ++i) {
        bitmap_[i] = 0xFFFFFFFFFFFFFFFF;
    }
}

void MemoryManager::MarkAllocated(uint64_t physical_address, size_t num_pages) {
    size_t start_page = physical_address / kPageSize;
    for (size_t i = 0; i < num_pages; ++i) {
        size_t page = start_page + i;
        if (page < kMaxPages) {
            bitmap_[page / 64] |= (1ULL << (page % 64));
        }
    }
}

void MemoryManager::MarkFree(uint64_t physical_address, size_t num_pages) {
    size_t start_page = physical_address / kPageSize;
    for (size_t i = 0; i < num_pages; ++i) {
        size_t page = start_page + i;
        if (page < kMaxPages) {
            bitmap_[page / 64] &= ~(1ULL << (page % 64));
        }
    }
}

void MemoryManager::Initialize(const BootInfo* boot_info) {
    uint8_t* p = boot_info->memory_map;
    uint8_t* map_end = p + boot_info->memory_map_size;

    size_t available_pages = 0;

    // memory_map_size を超えない範囲で、1要素(descriptor_size)ずつ進める
    while (p < map_end) {
        MemoryDescriptor* desc = (MemoryDescriptor*)p;
        
        // UEFIが「ここは自由に使っていいよ」と言っている標準メモリ空間
        if (desc->type == kEfiConventionalMemory) {
            // フリー状態（0）にする
            MarkFree(desc->physical_start, desc->number_of_pages);
            available_pages += desc->number_of_pages;
        }

        p += boot_info->descriptor_size;
    }

    // ★自作OSのド定番の罠・修正ポイント★
    // UEFIのメモリマップでは、物理アドレス 0x00000000 がしばしば「空き(ConventionalMemory)」として扱われます。
    // しかし、C/C++においてアドレス「0」は「nullptr」と同義であり、割り当て失敗のエラーを表します。
    // したがって、0番地は強制的に「使用済み」にマークし、newやmallocで返さないよう保護します。
    MarkAllocated(0, 1);

    console->Print("MemoryManager Initialized.\n");
    console->Print("Available RAM: ");
    console->PrintDec((available_pages * kPageSize) / kMiB);
    console->PrintLine(" MB");
}

uint64_t MemoryManager::Allocate(size_t num_pages) {
    size_t start_page = 0;
    size_t free_run_length = 0;

    // 今回は最もシンプルな先頭からの全探索で実装
    for (size_t page = 0; page < kMaxPages; ++page) {
        size_t idx = page / 64;
        int bit = page % 64;

        if ((bitmap_[idx] & (1ULL << bit)) == 0) {
            if (free_run_length == 0) {
                start_page = page;
            }
            free_run_length++;

            if (free_run_length == num_pages) {
                // 必要な連続ページが見つかったらそこをマークして返す
                MarkAllocated(start_page * kPageSize, num_pages);
                return start_page * kPageSize;
            }
        } else {
            // 使用済みのページに当たったらリセット
            free_run_length = 0;
        }
    }

    // メモリ枯渇
    return 0;
}

uint64_t MemoryManager::Allocate() {
    return Allocate(1);
}

void MemoryManager::Free(uint64_t physical_address, size_t num_pages) {
    MarkFree(physical_address, num_pages);
}
