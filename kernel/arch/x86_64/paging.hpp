// paging.hpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// ページディレクトリ等のエントリ数 (x86_64では各階層512個)
const size_t kPageDirectoryCount = 128; // 128 * 1GB = 128GB分を恒等マッピング

// CR3レジスタに設定するための関数 (アセンブリ命令を用いたインライン関数)
inline void SetCR3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

// ページングを初期化し、OS専用のページテーブルを構築・適用する
void InitializePaging();
void PrepareUserModeMappings();
bool AreUserModeMappingsReady();
