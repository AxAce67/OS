// paging.hpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// 1つの PML4 エントリは 512GB を担当する。
// 2エントリ分を恒等マッピングして 1TB をカバーする。
const size_t kMappedPML4EntryCount = 2;

// CR3レジスタに設定するための関数 (アセンブリ命令を用いたインライン関数)
inline void SetCR3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

// ページングを初期化し、OS専用のページテーブルを構築・適用する
void InitializePaging();
