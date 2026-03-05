# build.ps1
# WindowsネイティブでのUEFI（PE32+）ビルドスクリプト
param(
    [switch]$NoRun,
    [switch]$UseWhpx,
    [switch]$UseUsbTablet,
    [switch]$StrictDisk,
    [switch]$Smoke
)

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

function Resolve-ToolPath {
    param(
        [string]$ToolLabel,
        [string[]]$CandidatePaths,
        [string[]]$CommandNames
    )

    foreach ($p in $CandidatePaths) {
        if (-Not [string]::IsNullOrWhiteSpace($p) -and (Test-Path $p)) {
            return (Resolve-Path $p).Path
        }
    }
    foreach ($name in $CommandNames) {
        if ([string]::IsNullOrWhiteSpace($name)) {
            continue
        }
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($null -ne $cmd) {
            $resolved = $null
            if (-Not [string]::IsNullOrWhiteSpace($cmd.Source)) {
                $resolved = $cmd.Source
            } elseif ($cmd.PSObject.Properties.Name -contains "Path" -and -Not [string]::IsNullOrWhiteSpace($cmd.Path)) {
                $resolved = $cmd.Path
            } elseif ($cmd.PSObject.Properties.Name -contains "Definition" -and -Not [string]::IsNullOrWhiteSpace($cmd.Definition)) {
                $resolved = $cmd.Definition
            }

            if (-Not [string]::IsNullOrWhiteSpace($resolved) -and (Test-Path $resolved)) {
                return (Resolve-Path $resolved).Path
            }
        }
    }

    Write-Host "Error: $ToolLabel not found." -ForegroundColor Red
    if ($CandidatePaths.Count -gt 0) {
        Write-Host "Checked candidate paths:" -ForegroundColor Yellow
        foreach ($p in $CandidatePaths) {
            if (-Not [string]::IsNullOrWhiteSpace($p)) {
                Write-Host "  - $p" -ForegroundColor Yellow
            }
        }
    }
    if ($CommandNames.Count -gt 0) {
        Write-Host "Checked command names:" -ForegroundColor Yellow
        foreach ($n in $CommandNames) {
            if (-Not [string]::IsNullOrWhiteSpace($n)) {
                Write-Host "  - $n" -ForegroundColor Yellow
            }
        }
    }
    exit 1
}

function Ensure-ToolPath {
    param(
        [string]$ToolLabel,
        [string]$ToolPath
    )

    if ([string]::IsNullOrWhiteSpace($ToolPath) -or -Not (Test-Path $ToolPath)) {
        Write-Host "Error: resolved path for $ToolLabel is invalid: '$ToolPath'" -ForegroundColor Red
        exit 1
    }
}

if (-Not $NoRun) {
    Write-Host "Stopping existing QEMU processes..." -ForegroundColor Yellow
    Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue
}

# 1. コンパイラとリンカのパス（LLVM）
$clang = Resolve-ToolPath -ToolLabel "clang" -CandidatePaths @("C:\Program Files\LLVM\bin\clang.exe") -CommandNames @("clang", "clang.exe")
$lld_link = Resolve-ToolPath -ToolLabel "lld-link" -CandidatePaths @("C:\Program Files\LLVM\bin\lld-link.exe") -CommandNames @("lld-link", "lld-link.exe")
Ensure-ToolPath -ToolLabel "clang" -ToolPath $clang
Ensure-ToolPath -ToolLabel "lld-link" -ToolPath $lld_link

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
$ld_lld = Resolve-ToolPath -ToolLabel "ld.lld" -CandidatePaths @("C:\Program Files\LLVM\bin\ld.lld.exe") -CommandNames @("ld.lld", "ld.lld.exe")
Ensure-ToolPath -ToolLabel "ld.lld" -ToolPath $ld_lld
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

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/tab_completion.cpp -o shell_tab_completion.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell TabCompletion Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_event.cpp -o input_key_event.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyEvent Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_layout.cpp -o input_key_layout.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyLayout Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/hid_keyboard.cpp -o input_hid_keyboard.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input HID Keyboard Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/history.cpp -o input_history.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input History Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_ops.cpp -o input_line_ops.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineOps Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/selection.cpp -o input_selection.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Selection Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_render.cpp -o input_line_render.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineRender Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_editor.cpp -o input_line_editor.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineEditor Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_logic.cpp -o input_ime_logic.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Logic Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_engine.cpp -o input_ime_engine.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Engine Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_session.cpp -o input_ime_session.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Session Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_flow.cpp -o input_key_flow.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyFlow Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_handler.cpp -o input_key_handler.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyHandler Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_handler_exec.cpp -o input_key_handler_exec.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyHandler Exec Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/runtime_input_flow.cpp -o input_runtime_input_flow.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Runtime Flow Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/input_runtime_bridge.cpp -o input_runtime_bridge.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Runtime Bridge Compile Error!" -ForegroundColor Red; exit 1 }

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

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/ui/system_monitor.cpp -o ui_system_monitor.o
if ($LASTEXITCODE -ne 0) { Write-Host "UI System Monitor Compile Error!" -ForegroundColor Red; exit 1 }

& $clang -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/kernel.cpp -o kernel.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Compile Error!" -ForegroundColor Red; exit 1 }

# ELFファイルとしてリンク
& $ld_lld -m elf_x86_64 -z norelro --image-base 0x100000 --entry KernelMain -o kernel.elf kernel.o console.o mouse.o interrupt.o pic.o ps2.o pci.o xhci.o shell_commands.o shell_text.o shell_cmd_dispatch.o shell_cmd_core.o shell_cmd_fs.o shell_cmd_xhci.o shell_tab_completion.o input_key_event.o input_key_layout.o input_hid_keyboard.o input_history.o input_line_ops.o input_selection.o input_line_render.o input_line_editor.o input_ime_logic.o input_ime_engine.o input_ime_session.o input_key_flow.o input_key_handler.o input_key_handler_exec.o input_runtime_input_flow.o input_runtime_bridge.o interrupt_handler.o font.o memory.o paging.o apic.o timer.o window.o layer.o ui_system_monitor.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Link Error!" -ForegroundColor Red; exit 1 }

Write-Host "Build Success! -> main.efi & kernel.elf" -ForegroundColor Green
if ($NoRun) {
    Write-Host "Build completed. (NoRun mode: QEMU launch skipped)" -ForegroundColor Green
    exit 0
}

# 5. QEMU用のディスク構造の準備
if (Test-Path "disk") {
    try {
        Remove-Item -Recurse -Force "disk" -ErrorAction Stop
    }
    catch {
        if ($StrictDisk) {
            Write-Host "Error: failed to clean disk/ (file lock). aborting because -StrictDisk is enabled." -ForegroundColor Red
            exit 1
        }
        Write-Host "Warning: failed to clean disk/ (file lock). continuing with existing directory." -ForegroundColor Yellow
    }
}
New-Item -ItemType Directory -Force -Path "disk\EFI\BOOT" | Out-Null
try {
    Copy-Item "main.efi" -Destination "disk\EFI\BOOT\BOOTX64.EFI" -Force -ErrorAction Stop  # ブートローダー
    Copy-Item "kernel.elf" -Destination "disk\kernel.elf" -Force -ErrorAction Stop            # カーネル本体 (ELF)
    if (Test-Path "ime.dic") {
        Copy-Item "ime.dic" -Destination "disk\ime.dic" -Force -ErrorAction Stop
    }
    if (Test-Path "ime.learn") {
        Copy-Item "ime.learn" -Destination "disk\ime.learn" -Force -ErrorAction Stop
    }
}
catch {
    Write-Host "Error: failed to copy build artifacts into disk/." -ForegroundColor Red
    exit 1
}

Write-Host "Starting QEMU..." -ForegroundColor Cyan

$qemu = Resolve-ToolPath -ToolLabel "qemu-system-x86_64" -CandidatePaths @("C:\Program Files\qemu\qemu-system-x86_64.exe") -CommandNames @("qemu-system-x86_64", "qemu-system-x86_64.exe")
Ensure-ToolPath -ToolLabel "qemu-system-x86_64" -ToolPath $qemu

# 5. QEMUの実行
# ※Windows上での素のQEMUはデフォルトでレガシーBIOSなので、OVMF (UEFIファーム) を指定する必要がある。
# 今回は一番シンプルな形で仮実行してみる（OVMFがない場合のエラー対処は後ほど）

# QEMUインストールディレクトリにある可能性のあるOVMFを探す
$ovmf = $null
$ovmfCandidates = @(
    "OVMF.fd",
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

$hasOvmf = (-Not [string]::IsNullOrWhiteSpace($ovmf) -and (Test-Path $ovmf))

if (-Not $hasOvmf) {
    Write-Host "Error: OVMF firmware not found. UEFI boot is required." -ForegroundColor Red
    Write-Host "Checked paths:" -ForegroundColor Yellow
    foreach ($cand in $ovmfCandidates) {
        Write-Host "  - $cand" -ForegroundColor Yellow
    }
    exit 1
}
$ovmfPath = (Resolve-Path $ovmf).Path
Write-Host "Using OVMF: $ovmfPath" -ForegroundColor Cyan

$ovmfPathQuoted = $ovmfPath.Replace('"', '""')
$pflashArg = "if=pflash,format=raw,readonly=on,file=$ovmfPath"
$pflashArgForStartProcess = "if=pflash,format=raw,readonly=on,file=""$ovmfPathQuoted"""

$qemuArgs = @(
    "-m", "512M",
    "-machine", "q35",
    "-drive", $pflashArg
)
if ($UseUsbTablet) {
    $qemuArgs += @("-device", "qemu-xhci,msi=off", "-device", "usb-tablet")
}
$qemuArgs += @("-drive", "format=raw,file=fat:rw:disk")

if ($Smoke) {
    $smokeLogName = "qemu-smoke.log"
    $smokeLogPath = Join-Path $projectRoot $smokeLogName
    $smokeStdoutPath = Join-Path $projectRoot "qemu-smoke.stdout.log"
    $smokeStderrPath = Join-Path $projectRoot "qemu-smoke.stderr.log"
    if (Test-Path $smokeLogPath) {
        Remove-Item $smokeLogPath -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path $smokeStdoutPath) {
        Remove-Item $smokeStdoutPath -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path $smokeStderrPath) {
        Remove-Item $smokeStderrPath -Force -ErrorAction SilentlyContinue
    }

    $qemuSmokeArgs = @(
        "-m", "512M",
        "-machine", "q35",
        "-drive", $pflashArgForStartProcess
    )
    if ($UseUsbTablet) {
        $qemuSmokeArgs += @("-device", "qemu-xhci,msi=off", "-device", "usb-tablet")
    }
    $qemuSmokeArgs += @(
        "-drive", "format=raw,file=fat:rw:disk",
        "-display", "none",
        "-serial", "none",
        "-monitor", "none",
        "-debugcon", "file:$smokeLogName",
        "-global", "isa-debugcon.iobase=0xe9",
        "-no-reboot"
    )

    Write-Host "Running smoke boot check..." -ForegroundColor Cyan
    $qemuProc = Start-Process -FilePath $qemu `
                              -ArgumentList $qemuSmokeArgs `
                              -WorkingDirectory $projectRoot `
                              -RedirectStandardOutput $smokeStdoutPath `
                              -RedirectStandardError $smokeStderrPath `
                              -PassThru
    Start-Sleep -Seconds 12

    if (-Not $qemuProc.HasExited) {
        Stop-Process -Id $qemuProc.Id -Force -ErrorAction SilentlyContinue
    }

    if (-Not (Test-Path $smokeLogPath)) {
        Write-Host "Smoke Failed: qemu-smoke.log was not generated." -ForegroundColor Red
        if (Test-Path $smokeStderrPath) {
            Write-Host "qemu stderr:" -ForegroundColor Yellow
            Get-Content $smokeStderrPath | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
        }
        exit 1
    }

    $smokeLog = Get-Content $smokeLogPath -Raw -ErrorAction SilentlyContinue
    $hasSystemReady = (-Not [string]::IsNullOrEmpty($smokeLog)) -and $smokeLog.Contains("SYSTEM_READY")
    $hasPromptReady = (-Not [string]::IsNullOrEmpty($smokeLog)) -and $smokeLog.Contains("PROMPT_READY")

    if ($hasSystemReady -and $hasPromptReady) {
        Write-Host "Smoke Success: kernel reached ready state and prompt." -ForegroundColor Green
        exit 0
    }

    Write-Host "Smoke Failed: expected markers were not found." -ForegroundColor Red
    Write-Host "Expected: SYSTEM_READY and PROMPT_READY" -ForegroundColor Yellow
    Write-Host "Log file: $smokeLogPath" -ForegroundColor Yellow
    exit 1
}

& $qemu @qemuArgs
