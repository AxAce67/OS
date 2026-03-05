// memory.hpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "boot_info.h"

// 内部計算用の定数
const uint64_t kByte = 1;
const uint64_t kKiB = 1024 * kByte;
const uint64_t kMiB = 1024 * kKiB;
const uint64_t kGiB = 1024 * kMiB;

// 1ページのサイズは4KB
const uint64_t kPageSize = 4 * kKiB;

class MemoryManager {
public:
    // OSが管理する最大物理メモリ量 (128 GiBを想定)
    static const uint64_t kMaxPhysicalMemory = 128ULL * kGiB;
    static const size_t kMaxPages = kMaxPhysicalMemory / kPageSize;

    MemoryManager();

    // ブートローダーから渡されたUEFIのメモリマップを解析し、使用可能なページをOSの管理下に置く
    void Initialize(const BootInfo* boot_info);

    // 空いている物理ページを1つ(4KB)探し、その物理アドレスを返す
    // 確保できなかった場合は 0 を返す
    uint64_t Allocate();

    // 連続した複数ページを確保する
    uint64_t Allocate(size_t num_pages);

    // 使用終わりの物理アドレスを空き状態に戻す
    void Free(uint64_t physical_address, size_t num_pages = 1);

    // 現在の空きページ数を返す
    uint64_t CountFreePages() const;

private:
    // ビットマップ（1ビット = 1ページ）。0=空き、1=使用中
    // 128 GiB / 4KB = 33,554,432 ページ
    // 33,554,432 ページ / 64ビット = 524,288 個のuint64_t
    // 約4 MBの確保領域（.bssセクションに置かれるためELFファイルのサイズは増えない）
    uint64_t bitmap_[kMaxPages / 64];

    // 指定範囲のビットマップを指定状態にする内部関数
    void MarkAllocated(uint64_t physical_address, size_t num_pages);
    void MarkFree(uint64_t physical_address, size_t num_pages);
};

// カーネル全体で共通利用するMemoryManagerのインスタンス
extern MemoryManager* memory_manager;
