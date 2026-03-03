// paging.cpp
#include "paging.hpp"

// x86_64のページテーブルの各テーブルは必ず4KB (4096バイト) 境界に配置しなければならない。
// .bssセクション（初期値0）に配置されるためゼロクリアされている前提。
__attribute__((aligned(4096))) uint64_t pml4_table[512];
// kMappedPML4EntryCount 個の PML4 エントリを使い、各エントリ配下の PDP(512エントリ)を持つ。
__attribute__((aligned(4096))) uint64_t pdp_tables[kMappedPML4EntryCount][512];
// 1つのPDPエントリが1GBをマッピングする。(2MBページ × 512個)
// 合計で kMappedPML4EntryCount * 512GB を恒等マッピングする。
__attribute__((aligned(4096))) uint64_t page_directory[kMappedPML4EntryCount][512][512];

void InitializePaging() {
    // 1. 複数のPML4エントリを恒等マッピング用に構築する。
    for (size_t pml4_i = 0; pml4_i < kMappedPML4EntryCount; ++pml4_i) {
        // [設定値の意味] bit0: Present(有効), bit1: Read/Write(読み書き可)
        pml4_table[pml4_i] = reinterpret_cast<uint64_t>(&pdp_tables[pml4_i][0]) | 0x003;

        // 2. 各PDPエントリをページディレクトリ(第2層)へ向ける。
        for (size_t pdp_i = 0; pdp_i < 512; ++pdp_i) {
            pdp_tables[pml4_i][pdp_i] = reinterpret_cast<uint64_t>(&page_directory[pml4_i][pdp_i][0]) | 0x003;

            // 3. 各ページディレクトリに、2MBごとのブロック範囲を書き込む。
            for (size_t pd_i = 0; pd_i < 512; ++pd_i) {
                // 対象となる物理アドレス計算:
                //  (PML4 index * 512GB) + (PDP index * 1GB) + (PD index * 2MB)
                const uint64_t physical_addr =
                    (static_cast<uint64_t>(pml4_i) * 512ULL * 1024ULL * 1024ULL * 1024ULL) +
                    (static_cast<uint64_t>(pdp_i) * 1024ULL * 1024ULL * 1024ULL) +
                    (static_cast<uint64_t>(pd_i) * 2ULL * 1024ULL * 1024ULL);

                // [設定値の意味]
                // bit0: Present, bit1: Read/Write
                // bit7: Page Size (1にして Huge Page = 2MB単位 でマッピングする)
                page_directory[pml4_i][pdp_i][pd_i] = physical_addr | 0x083;
            }
        }
    }

    // 4. 定義したPML4テーブルの先頭（物理アドレス）をCPUのCR3にセットする。
    // ※ 現在はUEFIが構築した恒等マッピング上にあるため、仮想アドレス＝物理アドレスになっている。
    // ※ したがって、アドレスをそのままCR3に書き込んでも大丈夫。
    SetCR3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}
