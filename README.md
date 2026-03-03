# Native OS (C++ Edition)

フルスクラッチで開発中のベアメタルOSプロジェクトです。
UEFIブートローダーから起動し、64ビットモード環境へ移行後、C++で記述された独自カーネルを実行します。

## 概要

このプロジェクトは、OS自作の基礎から順を追って実践し、最終的にGUIウィンドウシステムとC++環境を提供する本格的なオペレーティングシステムの構築を目指しています。
VGAやBIOSコールなどのレガシーな仕組みを使用せず、UEFI GOPによる高解像度グラフィックスと、x86_64アーキテクチャの標準的なページング、割り込み制御（PIC/IDT）を採用したモダンな構成基盤を持ちます。

## 現在の実装機能

*   **UEFIブートローダー**: `boot/main.c` (UEFI規格に準拠し、C言語で記述)
    *   メインメモリ上のマップ領域(Memory Map)の取得
    *   カーネルファイル(ELF形式)のファイルシステムからの読み込みとメモリ展開
    *   UEFI GOP(Graphics Output Protocol)によるフレームバッファ情報の取得
    *   `ExitBootServices` を呼び出し、UEFI支配下からOSネイティブ環境への制御移行
*   **C++カーネル環境**: `kernel/kernel.cpp` ほか
    *   独自の `new` / `delete` 演算子オーバーロード（配置new、動的メモリ確保の基礎）
    *   クラス機構を利用したオブジェクト指向によるOS内部設計
*   **メモリ・割り込み管理**
    *   `Paging`: 4段ページテーブル(PML4/PDP/PD/PT)による仮想メモリ空間の構築
    *   `GDT` / `IDT`: セグメンテーションと割り込みディスクリプタ定義
    *   `PIC` / `PS/2`: 割り込みコントローラの設定とキーボード/マウス割り込みの有効化
    *   `Local APIC Timer`: 周期タイマ割り込みによるカーネルtickカウント
*   **GUI基盤・レイヤーシステム**
    *   `Console`: フォントデータ(`font.h`)を利用した文字列描画
    *   `Window` / `Layer` / `LayerManager`: Zオーダー付きのコンポジット（合成表示）システム
    *   部分描画最適化（ローカルリフレッシュ）によるマウスの滑らかなリアルタイム描画
    *   入力イベント層は `relative/absolute` 両対応のメッセージ形式に拡張済み（absolute本実装は今後）
*   **最小シェル**
    *   `os> ` プロンプトと1行入力（Backspace対応）
    *   組み込みコマンド: `help`, `clear`, `tick`, `time`, `mem`, `uptime`, `echo`, `reboot`
    *   履歴/補完: `history`, `clearhistory`, `Tab補完`, `↑/↓` 履歴移動
    *   シェル拡張: `set`, `alias`, `repeat`, `layout`, `inputstat`, `about`
    *   ブート時RAMファイルシステム: `ls`, `cat`

## 起動方法（ビルドと実行）

本プロジェクトは Windows 環境において、LLVM（Clang/LLD）によるクロスコンパイルと、QEMUエミュレータ上のOVMF（UEFIファームウェア）環境で実行します。

### 必要要件（前提環境）

1.  **Windows OS** (PowerShellが実行可能な環境)
2.  **LLVM (Clang / LLD)**
    *   C/C++ソースのコンパイルおよびELF形式へのリンクに使用します。
    *   PATHが通っており、コマンドプロンプトやPowerShellから `clang` と `ld.lld` が認識される必要があります。
3.  **QEMU (qemu-system-x86_64)**
    *   作成したOSイメージを動かすための仮想マシン(エミュレータ)です。
    *   PATHが通っている必要があります。
4.  **OVMF_CODE.fd / OVMF_VARS.fd**
    *   QEMU上でUEFI環境を再現するためのオープンソースファームウェアです。
    *   プロジェクトディレクトリ直下（`c:\os\`）に配置されていることを前提としています。

### プロジェクトの実行プロセス

OSのビルドとQEMUでの起動は、すべて `scripts/build.ps1` PowerShellスクリプトにまとめられています。

1.  PowerShell または ターミナルを開きます。
2.  プロジェクトディレクトリ (`c:\os` など) に移動します。
3.  以下のコマンドを実行します。

```powershell
.\scripts\build.ps1
```

**スクリプトが自動で行うこと:**
1.  稼働中のQEMUプロセスがあれば停止します。
2.  `main.c` をコンパイルし、UEFIアプリケーション `main.efi` (ブートローダー) を作成します。
3.  各種 OS のC/C++ソースコード（`kernel.cpp`, `layer.cpp` 等）をコンパイルし、1つの実行可能ファイル `kernel.elf` にリンクします。
4.  QEMU向けの仮想ディスク(FAT形式のディレクトリマウント)を構成し、`EFI\BOOT\BOOTX64.EFI` としてブートローダーを配置します。
5.  QEMUを起動し、作成したOSイメージを実行します。

### 停止方法

*   QEMUのウィンドウを閉じるか、ターミナルで `Ctrl + C` を押してバッチプロセスを停止してください。
*   残ってしまったQEMUプロセスは、PowerShellから `Stop-Process -Name qemu-system-x86_64` で強制終了できます。

## ディレクトリと主要ファイル構成

*   `boot/`: UEFIブートローダーとブート情報構造体（`main.c`, `efi.h`, `boot_info.h` など）。
*   `kernel/kernel.cpp`: C++ カーネルのエントリーポイント（`KernelMain`）。
*   `kernel/arch/x86_64/`: 割り込み・PIC/PS2・ページング・APIC/Timer などCPU依存層。
*   `kernel/memory/`: 物理メモリ管理。
*   `kernel/graphics/`: Console / Window / Layer / Mouse / Font。
*   `scripts/build.ps1`: 全自動ビルド＆起動スクリプト。
*   `tools/`: 開発補助スクリプト（`generate_font.py`）。

## 本格OSに向けた直近マイルストーン

1. **ブート安定化**
   * UEFI APIエラーハンドリング強化
   * `ExitBootServices` リトライ対応
2. **メモリ基盤の再設計**
   * 物理メモリ管理とページング範囲の整合
   * カーネルヒープ（`new/delete`）の厳密化
3. **プロセス基盤**
   * タイマ割り込み（LAPIC/HPET）
   * スケジューラとユーザ空間分離（ring3）
4. **I/O基盤**
   * キーボード/ストレージ/ファイルシステム
   * システムコール境界の確立

## マウス挙動について（現状）

* 現在のカーネル入力は `PS/2` 相対移動を使用しているため、WindowsデスクトップからQEMUへ入った瞬間の「位置完全一致」はできません。
* 将来的に自然な挙動（入った位置からそのまま）を実現するには、`USB HID Absolute Pointer` のドライバが必要です。
* 先行対応として、カーネル内部の入力イベントは absolute 座標を受け取れる設計に更新済みです。

# OS
