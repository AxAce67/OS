# build.ps1
# WindowsネイティブでのUEFI（PE32+）ビルドスクリプト

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

Write-Host "Stopping existing QEMU processes..." -ForegroundColor Yellow
Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue

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
& $ld_lld -m elf_x86_64 -z norelro --image-base 0x100000 --entry KernelMain -o kernel.elf kernel.o console.o mouse.o interrupt.o pic.o ps2.o pci.o xhci.o shell_commands.o shell_text.o interrupt_handler.o font.o memory.o paging.o apic.o timer.o window.o layer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Link Error!" -ForegroundColor Red; exit 1 }

Write-Host "Build Success! -> main.efi & kernel.elf" -ForegroundColor Green

# 5. QEMU用のディスク構造の準備
if (Test-Path "disk") { Remove-Item -Recurse -Force "disk" }
New-Item -ItemType Directory -Path "disk\EFI\BOOT" | Out-Null
Copy-Item "main.efi" -Destination "disk\EFI\BOOT\BOOTX64.EFI"   # ブートローダー
Copy-Item "kernel.elf" -Destination "disk\kernel.elf"           # カーネル本体 (ELF)

Write-Host "Starting QEMU..." -ForegroundColor Cyan

# 5. QEMUの実行
# ※Windows上での素のQEMUはデフォルトでレガシーBIOSなので、OVMF (UEFIファーム) を指定する必要がある。
# 今回は一番シンプルな形で仮実行してみる（OVMFがない場合のエラー対処は後ほど）

# QEMUインストールディレクトリにある可能性のあるOVMFを探す
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (-Not (Test-Path $ovmf)) {
    # フォールバック用の探し方
    $ovmf = "$env:USERPROFILE\OVMF.fd"
}

# ファイルがある場合はローカルにコピーして使う（アクセス権限の問題回避）
if (Test-Path $ovmf) {
    if (-Not (Test-Path "OVMF.fd")) {
        Copy-Item $ovmf -Destination "OVMF.fd"
    }
    & $qemu -m 512M `
        -display "gtk,show-menubar=off" `
        -device qemu-xhci `
        -device usb-tablet `
        -pflash "OVMF.fd" `
        -drive "format=raw,file=fat:rw:disk"
}
else {
    Write-Host "Warning: OVMF.fd (UEFI BIOS) not found. QEMU might boot in Legacy BIOS mode." -ForegroundColor Yellow
    # 警告を出しつつFATディレクトリを指定して通常起動
    & $qemu -m 512M `
        -display "gtk,show-menubar=off" `
        -device qemu-xhci `
        -device usb-tablet `
        -drive "format=raw,file=fat:rw:disk"
}
