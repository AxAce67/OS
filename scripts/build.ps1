# build.ps1
# WindowsネイティブでのUEFI（PE32+）ビルドスクリプト
param(
    [switch]$NoRun,
    [switch]$UseWhpx,
    [switch]$UseUsbTablet
)

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

if (-Not $NoRun) {
    Write-Host "Stopping existing QEMU processes..." -ForegroundColor Yellow
    Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue
}

# 1. コンパイラとリンカのパス（LLVM）
$clang = "C:\Program Files\LLVM\bin\clang.exe"
$lld_link = "C:\Program Files\LLVM\bin\lld-link.exe"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"

# OVFMF.fd (UEFI BIOS ROM) は本来QEMU付属のものか、EDK2のビルド済みイメージが必要
# ここではQEMUパッケージなどに一部付属している想定（もしくは警告無視）で起動テストします

Write-Host "Compiling main.c -> main.efi..." -ForegroundColor Cyan

# 2. Clangによるコンパイル (オブジェクトファイル生成)
# ターゲット: Windows環境のx86_64ターゲットを指定。UEFIは標準ライブラリを持たない(-ffreestanding)
& $clang -target x86_64-pc-win32-coff -mno-red-zone -fno-stack-protector -fshort-wchar -Wall -I boot -c boot/main.c -o main.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compile Error!" -ForegroundColor Red
    exit 1
}

# 3. LLD-LINKによるリンク (UEFIアプリケーション .efi を生成)
# /subsystem:efi_application が EFI_APPLICATION の意味
# /entry:efi_main がエントリポイント指定
& $lld_link /subsystem:efi_application /entry:efi_main /out:main.efi main.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "Link Error!" -ForegroundColor Red
    exit 1
}

Write-Host "Compiling kernel.c -> kernel.elf..." -ForegroundColor Cyan

# 4. カーネル本体とフォントのコンパイルとリンク (ELF形式)
$ld_lld = "C:\Program Files\LLVM\bin\ld.lld.exe"
$commonKernelIncludes = @(
    "-I", "boot",
    "-I", "kernel",
    "-I", "kernel/arch/x86_64",
    "-I", "kernel/memory",
    "-I", "kernel/graphics"
)

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fshort-wchar -Wall @commonKernelIncludes -c kernel/graphics/font.c -o font.o
if ($LASTEXITCODE -ne 0) { Write-Host "Font Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/console.cpp -o console.o
if ($LASTEXITCODE -ne 0) { Write-Host "Console Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/mouse.cpp -o mouse.o
if ($LASTEXITCODE -ne 0) { Write-Host "Mouse Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/interrupt.cpp -o interrupt.o
if ($LASTEXITCODE -ne 0) { Write-Host "Interrupt Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/pic.cpp -o pic.o
if ($LASTEXITCODE -ne 0) { Write-Host "PIC Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/ps2.cpp -o ps2.o
if ($LASTEXITCODE -ne 0) { Write-Host "PS2 Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/pci.cpp -o pci.o
if ($LASTEXITCODE -ne 0) { Write-Host "PCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/usb/xhci.cpp -o xhci.o
if ($LASTEXITCODE -ne 0) { Write-Host "xHCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/commands.cpp -o shell_commands.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Commands Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/text.cpp -o shell_text.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Text Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_dispatch.cpp -o shell_cmd_dispatch.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Dispatch Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_core.cpp -o shell_cmd_core.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Core Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_fs.cpp -o shell_cmd_fs.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell FS Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_xhci.cpp -o shell_cmd_xhci.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell xHCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_event.cpp -o input_key_event.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyEvent Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_layout.cpp -o input_key_layout.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyLayout Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/hid_keyboard.cpp -o input_hid_keyboard.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input HID Keyboard Compile Error!" -ForegroundColor Red; exit 1 }

# 割り込みハンドラはSSEレジスタを使わないように -mgeneral-regs-only を指定する
& $clang -target x86_64-elf -mno-red-zone -mgeneral-regs-only -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/interrupt_handler.cpp -o interrupt_handler.o
if ($LASTEXITCODE -ne 0) { Write-Host "Interrupt Handler Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/memory/memory.cpp -o memory.o
if ($LASTEXITCODE -ne 0) { Write-Host "Memory Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/paging.cpp -o paging.o
if ($LASTEXITCODE -ne 0) { Write-Host "Paging Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/apic.cpp -o apic.o
if ($LASTEXITCODE -ne 0) { Write-Host "APIC Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/timer.cpp -o timer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Timer Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/window.cpp -o window.o
if ($LASTEXITCODE -ne 0) { Write-Host "Window Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/layer.cpp -o layer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Layer Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/kernel.cpp -o kernel.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Compile Error!" -ForegroundColor Red; exit 1 }

# ELFファイルとしてリンク
& $ld_lld -m elf_x86_64 -z norelro --image-base 0x100000 --entry KernelMain -o kernel.elf kernel.o console.o mouse.o interrupt.o pic.o ps2.o pci.o xhci.o shell_commands.o shell_text.o shell_cmd_dispatch.o shell_cmd_core.o shell_cmd_fs.o shell_cmd_xhci.o input_key_event.o input_key_layout.o input_hid_keyboard.o interrupt_handler.o font.o memory.o paging.o apic.o timer.o window.o layer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Link Error!" -ForegroundColor Red; exit 1 }

Write-Host "Build Success! -> main.efi & kernel.elf" -ForegroundColor Green

# 5. QEMU用のディスク構造の準備
if (Test-Path "disk") {
    try {
        Remove-Item -Recurse -Force "disk" -ErrorAction Stop
    }
    catch {
        Write-Host "Warning: failed to clean disk/ (file lock). continuing with existing directory." -ForegroundColor Yellow
    }
}
New-Item -ItemType Directory -Force -Path "disk\EFI\BOOT" | Out-Null
Copy-Item "main.efi" -Destination "disk\EFI\BOOT\BOOTX64.EFI"   # ブートローダー
Copy-Item "kernel.elf" -Destination "disk\kernel.elf"           # カーネル本体 (ELF)
if (Test-Path "ime.dic") {
    Copy-Item "ime.dic" -Destination "disk\ime.dic"
}
if (Test-Path "ime.learn") {
    Copy-Item "ime.learn" -Destination "disk\ime.learn"
}

if ($NoRun) {
    Write-Host "Build completed. (NoRun mode: QEMU launch skipped)" -ForegroundColor Green
    exit 0
}

Write-Host "Starting QEMU..." -ForegroundColor Cyan

# 5. QEMUの実行
# ※Windows上での素のQEMUはデフォルトでレガシーBIOSなので、OVMF (UEFIファーム) を指定する必要がある。
# 今回は一番シンプルな形で仮実行してみる（OVMFがない場合のエラー対処は後ほど）

# QEMUインストールディレクトリにある可能性のあるOVMFを探す
$ovmf = $null
$ovmfCandidates = @(
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
)
if (-Not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
    $ovmfCandidates += "$env:USERPROFILE\OVMF.fd"
}
foreach ($cand in $ovmfCandidates) {
    if (-Not [string]::IsNullOrWhiteSpace($cand) -and (Test-Path $cand)) {
        $ovmf = $cand
        break
    }
}

# ファイルがある場合はローカルにコピーして使う（アクセス権限の問題回避）
$accelArg = if ($UseWhpx) { "whpx" } else { "tcg" }
$hasOvmf = (-Not [string]::IsNullOrWhiteSpace($ovmf) -and (Test-Path $ovmf))

if ($hasOvmf) {
    if (-Not (Test-Path "OVMF.fd")) {
        Copy-Item $ovmf -Destination "OVMF.fd"
    }
} else {
    Write-Host "Warning: OVMF.fd (UEFI BIOS) not found. QEMU might boot in Legacy BIOS mode." -ForegroundColor Yellow
}

$qemuArgs = @(
    "-m", "512M",
    "-machine", "q35",
    "-accel", $accelArg,
    "-k", "ja"
)

if ($UseUsbTablet) {
    # xHCI + WHPX環境でMSIトラブルが出るため、MSIを明示的に無効化する
    $qemuArgs += @("-device", "qemu-xhci,msi=off")
    $qemuArgs += @("-device", "usb-tablet")
}

if ($hasOvmf) {
    $qemuArgs += @("-drive", "if=pflash,format=raw,readonly=on,file=OVMF.fd")
}
$qemuArgs += @("-drive", "format=raw,file=fat:rw:disk")
$qemuArgs = $qemuArgs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

# Foreground run: keep terminal attached so behavior is obvious.
& $qemu @qemuArgs
