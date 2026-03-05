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
    *   `Window Manager v1`: `Desktop + Taskbar + Terminal` の初期UI構成（ターミナル枠のドラッグ移動対応）
    *   `Window Manager v2`: `System` ウィンドウ追加、クリックで前面化（フォーカス切替）と複数ウィンドウのドラッグ移動
    *   部分描画最適化（ローカルリフレッシュ）によるマウスの滑らかなリアルタイム描画
    *   入力イベント層は `relative/absolute` 両対応のメッセージ形式に拡張済み（absolute本実装は今後）
*   **最小シェル**
    *   `os> ` プロンプトと1行入力（Backspace対応）
    *   組み込みコマンド: `help`, `clear`, `tick`, `time`, `mem`, `uptime`, `echo`, `reboot`
    *   履歴/補完: `history`, `clearhistory`, `Tab補完`, `↑/↓` 履歴移動
    *   シェル拡張: `set`, `alias`, `repeat`, `layout`, `inputstat`, `about`
    *   IME候補操作: `Space`/`↑`/`↓` 候補選択 / `Enter` 確定 / `Esc` キャンセル（`Space` は完全一致優先、なければ前方一致候補）
    *   IME学習運用: `ime stat`, `ime export`, `ime save [path]`, `ime import [path]`, `ime resetlearn`
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

QEMUを起動せずにビルドだけ確認したい場合は以下を使用します。

```powershell
.\scripts\build.ps1 -NoRun
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

## 起動失敗時の切り分け

1. まず `.\scripts\build.ps1 -NoRun` でビルドだけ通るか確認する  
2. 起動時に `BOOT ERROR [Bxxx]` が出る場合は、コードで箇所を特定する
   - `B10x`: GOP/画面初期化
   - `B11x`: ファイルシステム/ボリューム取得
   - `B12x`: `kernel.elf` 読み込み
   - `B13x`: ELF検証/展開
   - `B14x`: `GetMemoryMap` / `ExitBootServices`
3. カーネル側で停止した場合は `KERNEL PANIC [Kxxx]` を確認する
   - `K001`: 動的メモリ確保失敗
   - `K002`: ブート情報不正
4. QEMUが起動直後に閉じる場合は、まず既存プロセスを止めて再実行する  
   `Stop-Process -Name qemu-system-x86_64 -Force`

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

## 土台整理完了チェック（進行中）

- [x] 入力runtime文脈の bridge 化（`input_runtime_bridge`）
- [x] マウス/キーボードハンドラの「状態組み立て」と「実行呼び出し」分離
- [x] compositor更新処理の責務分割（focus/drag/pointer/system）
- [x] イベントループの待機・dispatch分割
- [x] `kernel.cpp` から入力ハンドラ実体を `kernel/input/` へ移設（entry helper化）
- [x] callback束構造体で runtime 呼び出し引数を更に圧縮

## 堅牢化チェック（進行中）

- [x] ELFローダの範囲/オーバーフロー検証を追加
- [x] Boot FS 読込の `EFI_BUFFER_TOO_SMALL` 再試行に対応
- [x] OOM時に停止前の診断メッセージを表示
- [x] キュー操作を `event_queue` API へ集約（将来SMP置換準備）

## マウス挙動について（現状）

* 現在のカーネル入力は `PS/2` 相対移動を使用しているため、WindowsデスクトップからQEMUへ入った瞬間の「位置完全一致」はできません。
* 将来的に自然な挙動（入った位置からそのまま）を実現するには、`USB HID Absolute Pointer` のドライバが必要です。
* 先行対応として、カーネル内部の入力イベントは absolute 座標を受け取れる設計に更新済みです。

## 開発運用ルール

機能追加ごとに以下を1セットで実施します。

1. `.\scripts\build.ps1` でビルドし、起動エラーがないことを確認
2. 主要コマンドを手動で最小確認（下のチェックリスト）
3. 変更ファイルだけを `git add`
4. `git commit -m "..."`（1機能1コミット）
5. `git push`（`main` 反映）

## 手動テストチェックリスト（シェル）

### 最小入力回帰セット（毎コミット）

- 起動後に `os:/>` が表示される
- 英数入力が1文字ずつ正しく反映される（重なり/欠けなし）
- `Backspace` と `Delete` が見た目と内部状態で一致
- `←/→` でカーソル移動、`↑/↓` で履歴の戻る/進む
- `Enter` でコマンド実行後、次プロンプトで入力状態が初期化される
- `layout jp` で記号キー（`@`, `[`, `]`, `^`, `\` など）がJP配列どおり入力される
- IME有効時に `Space` 候補開始、`↑/↓` 候補移動、`Enter` 確定、`Esc` キャンセル
- マウス移動・ウィンドウドラッグ中に入力が破綻しない（フリーズ/点滅ループなし）

### 起動

- 起動直後に `os:/ >` が表示される
- キーボード入力が反映される
- マウス移動で画面が壊れない
- ターミナルウィンドウのタイトルバーをドラッグして移動できる
- `System` ウィンドウをクリックすると前面化され、タイトルバーをドラッグして移動できる

### UI/コンポジタ回帰

- ターミナルとSystemのフォーカス切替でタイトルバー色が即時反映される
- ウィンドウドラッグ中にカーソルが最前面を維持し、描画破綻しない
- ドラッグ停止直後にウィンドウ位置が確定し、残像が残らない
- マウス移動中に `System` 更新で大きな引っかかりが連続しない
- 連続入力中（キー入力＋マウス移動）でもプロンプト描画が崩れない

### 基本コマンド

- `help` が表示される
- `clear` で画面クリア
- `tick`, `time`, `uptime`, `mem` が表示される
- `about` が表示される

### 履歴・編集

- 文字入力、`Backspace`、`Delete` が見た目と内部状態で一致
- `↑/↓` で履歴を戻る/進む
- `history`, `clearhistory` が動作する
- `Tab` 補完（コマンド/ファイル）が動作する
- IME候補表示中に `Space`/`↑`/`↓` で候補移動、`Enter` で確定、`Esc` でキャンセルできる
- `Space` で候補起動時、まず完全一致キーを参照し、なければ前方一致キー群から候補を合成する（キー長が長い候補を優先）
- 候補確定を繰り返すと、同じキーで次回候補を開いたときに学習済み候補が優先される（セッション内）
- `ime save` で学習内容をファイル出力し、`ime import` で再読込できる
- `ime.learn` をプロジェクト直下に置くと起動時に自動読込される（`build.ps1` が `disk/ime.learn` へコピー）

### FSコマンド

- `pwd`, `cd`, `mkdir`, `touch` が動作
- `write`, `append`, `cat`, `stat` が動作
- `ls` 表示順が安定している
- `cp`, `mv`, `rm`, `rmdir` の失敗系（存在しない/競合/保護対象）で適切なエラー表示

### 設定コマンド

- `repeat on/off` が反映される
- `layout us/jp` が反映される
- `set`, `alias` の保存と表示が動作する

### xHCI系（対応環境のみ）

- `xhciinfo` でコントローラ情報が表示される
- `usbports` が表示される
- `xhciauto on/off` が切り替わる

# OS
