// paging.cpp
#include "paging.hpp"

// x86_64のページテーブルの各テーブルは必ず4KB (4096バイト) 境界に配置しなければならない。
// .bssセクション（初期値0）に配置されるためゼロクリアされている前提
__attribute__((aligned(4096))) uint64_t pml4_table[512];
__attribute__((aligned(4096))) uint64_t pdp_table[512];

// 1つのPDPエントリが1GBをマッピングする。(2MBページ × 512個)
// kPageDirectoryCount GB分の仮想メモリを物理メモリとそのまま（恒等）マッピングする
__attribute__((aligned(4096))) uint64_t page_directory[kPageDirectoryCount][512];

// xHCI MMIO が 0xC000000000 (768GB) 付近に割り当てられる環境向けの最小追加マップ。
// PML4[1] の PDP[256] (= 512GB + 256GB) に 1GB の2MBページ群を張る。
__attribute__((aligned(4096))) uint64_t pdp_table_high[512];
__attribute__((aligned(4096))) uint64_t page_directory_high_mmio[512];

void InitializePaging() {
    // 1. PML4(第4層)の先頭の1エントリを、PDP(第3層)へ向ける
    // [設定値の意味] bit0: Present(有効), bit1: Read/Write(読み書き可)
    pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x003;

    // 2. PDPテーブルの先頭kPageDirectoryCount個をそれぞれのページディレクトリ(第2層)へ向ける
    for (size_t i = 0; i < kPageDirectoryCount; ++i) {
        // [設定値の意味] bit0: Present, bit1: Read/Write
        pdp_table[i] = reinterpret_cast<uint64_t>(&page_directory[i][0]) | 0x003;

        // 3. 各ページディレクトリに、2MBごとのブロック範囲を書き込む
        for (size_t j = 0; j < 512; ++j) {
            // 対象となる物理アドレス計算
            // i=0, j=0 なら 0MB
            // i=0, j=1 なら 2MB ...
            // i=1, j=0 なら 1024MB(1GB) ...
            uint64_t physical_addr = i * 1024ULL * 1024ULL * 1024ULL + j * 2ULL * 1024ULL * 1024ULL;

            // [設定値の意味]
            // bit0: Present, bit1: Read/Write
            // bit7: Page Size (1にして Huge Page = 2MB単位 でマッピングする)
            page_directory[i][j] = physical_addr | 0x083;
        }
    }

    // 追加: 0xC000000000 〜 0xC03FFFFFFF (1GB) を恒等マッピング。
    pml4_table[1] = reinterpret_cast<uint64_t>(&pdp_table_high[0]) | 0x003;
    pdp_table_high[256] = reinterpret_cast<uint64_t>(&page_directory_high_mmio[0]) | 0x003;
    const uint64_t high_mmio_base = 0x000000C000000000ULL;
    for (size_t j = 0; j < 512; ++j) {
        page_directory_high_mmio[j] = (high_mmio_base + j * 2ULL * 1024ULL * 1024ULL) | 0x083;
    }

    // 4. 定義したPML4テーブルの先頭（物理アドレス）をCPUのCR3にセットする。
    // ※ 現在はUEFIが構築した恒等マッピング上にあるため、仮想アドレス＝物理アドレスになっている。
    // ※ したがって、アドレスをそのままCR3に書き込んでも大丈夫。
    SetCR3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}
