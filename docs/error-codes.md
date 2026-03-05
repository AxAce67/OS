# Native OS Error Codes

最終更新: 2026-03-05

## 1. Boot Loader (`BOOT ERROR [Bxxx]`)

`boot/main.c` の `BootFatal(...)` で出力されるコードです。

- `B101`: GOP の取得失敗
- `B102`: GOP の PixelFormat 非対応
- `B110`: LoadedImage protocol 取得失敗
- `B111`: SimpleFileSystem protocol 取得失敗
- `B112`: ルートボリュームオープン失敗
- `B120`: `kernel.elf` オープン失敗
- `B121`: `kernel.elf` の file info 取得失敗
- `B122`: `kernel.elf` バッファ確保失敗
- `B123`: `kernel.elf` 読み込み失敗
- `B124`: Boot FS 構築失敗
- `B130`: ELF 検証失敗（unsafe layout）
- `B131`: ELF ロード用ページ確保失敗
- `B132`: ELF エントリポイントがロード範囲外
- `B140`: `GetMemoryMap` の期待外ステータス
- `B141`: Memory Map バッファ確保失敗
- `B142`: `GetMemoryMap` 再取得失敗
- `B143`: `ExitBootServices` 失敗（`EFI_INVALID_PARAMETER` 以外）

## 2. Kernel (`KERNEL PANIC [Kxxx]`)

`kernel/kernel.cpp` の `KernelPanic(...)` または関連 panic 出力で表示されるコードです。

- `K001`: 動的メモリ確保失敗（OOM）
- `K002`: ブート情報不正（`BootInfo` / `frame_buffer_config` が無効）

## 3. Troubleshooting Flow

1. `.\scripts\build.ps1 -NoRun` でビルドが通るか確認
2. 起動失敗時の表示コードを控える（`Bxxx` または `Kxxx`）
3. このファイルでコードのカテゴリを確認して、該当層（Boot / Kernel）を先に調査

