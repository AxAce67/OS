# build.ps1
# WindowsネイティブでのUEFI（PE32+）ビルドスクリプト
param(
    [switch]$NoRun,
    [switch]$UseWhpx,
    [switch]$UseUsbTablet,
    [switch]$NoUsbTablet,
    [switch]$StrictDisk,
    [switch]$Smoke,
    [switch]$NoClipBridge
)

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

$enableUsbTablet = $true
if ($NoUsbTablet) {
    $enableUsbTablet = $false
}
elseif ($UseUsbTablet) {
    $enableUsbTablet = $true
}

function Test-ToolExists {
    param(
        [string]$ToolLabel,
        [string]$ToolPath
    )
    if ([string]::IsNullOrWhiteSpace($ToolPath) -or -Not (Test-Path $ToolPath)) {
        Write-Host "Error: $ToolLabel not found at '$ToolPath'" -ForegroundColor Red
        exit 1
    }
}

function Write-HostClipboardSnapshot {
    param(
        [string]$OutputPath,
        [int]$MaxChars = 120
    )
    try {
        $clip = Get-Clipboard -Raw -ErrorAction Stop
        if ($null -eq $clip) { return }
        $clip = $clip -replace "(\r\n|\r|\n)+", " "
        $clip = $clip -replace "\t+", " "
        $clip = $clip.Trim()
        if ([string]::IsNullOrWhiteSpace($clip)) { return }

        $sb = New-Object System.Text.StringBuilder
        for ($i = 0; $i -lt $clip.Length -and $sb.Length -lt $MaxChars; $i++) {
            $ch = $clip[$i]
            $code = [int][char]$ch
            if ($code -ge 32 -and $code -le 126) {
                [void]$sb.Append($ch)
            }
            else {
                [void]$sb.Append("?")
            }
        }
        $sanitized = $sb.ToString().Trim()
        if ([string]::IsNullOrWhiteSpace($sanitized)) { return }
        [System.IO.File]::WriteAllText($OutputPath, $sanitized, [System.Text.Encoding]::ASCII)
    }
    catch {
        # Clipboard access is optional; continue build/run flow.
    }
}

function New-Ring3SampleBinary {
    param(
        [string]$OutputPath
    )
    if (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
        $OutputPath = Join-Path $projectRoot $OutputPath
    }
    $msgText = "[ring3-file] hello from runfile`n"
    $msgBytes = [System.Text.Encoding]::ASCII.GetBytes($msgText)
    $msgLen = [uint64]$msgBytes.Length
    $imgPart1 = [byte[]](0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00) # mov rax,1
    $imgPart2 = [byte[]](0x48, 0x8D, 0x3D, 0x2C, 0x00, 0x00, 0x00)                 # lea rdi,[rip+44]
    $imgPart3 = [byte[]](0x48, 0xBE)                                           # mov rsi,imm64
    $imgPart4 = [System.BitConverter]::GetBytes($msgLen)
    $imgPart5 = [byte[]](0x48, 0x31, 0xD2, 0x48, 0x31, 0xC9, 0xCD, 0x80)           # xor/xor/int80
    $imgPart6 = [byte[]](0x48, 0xB8, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00) # mov rax,4
    $imgPart7 = [byte[]](0x48, 0x31, 0xFF, 0x48, 0x31, 0xF6, 0x48, 0x31, 0xD2, 0x48, 0x31, 0xC9, 0xCD, 0x80, 0xEB, 0xFE)

    $imageLen = $imgPart1.Length + $imgPart2.Length + $imgPart3.Length + $imgPart4.Length + $imgPart5.Length + $imgPart6.Length + $imgPart7.Length + $msgBytes.Length
    $imageBytes = New-Object byte[] $imageLen
    $off = 0
    [Array]::Copy($imgPart1, 0, $imageBytes, $off, $imgPart1.Length); $off += $imgPart1.Length
    [Array]::Copy($imgPart2, 0, $imageBytes, $off, $imgPart2.Length); $off += $imgPart2.Length
    [Array]::Copy($imgPart3, 0, $imageBytes, $off, $imgPart3.Length); $off += $imgPart3.Length
    [Array]::Copy($imgPart4, 0, $imageBytes, $off, $imgPart4.Length); $off += $imgPart4.Length
    [Array]::Copy($imgPart5, 0, $imageBytes, $off, $imgPart5.Length); $off += $imgPart5.Length
    [Array]::Copy($imgPart6, 0, $imageBytes, $off, $imgPart6.Length); $off += $imgPart6.Length
    [Array]::Copy($imgPart7, 0, $imageBytes, $off, $imgPart7.Length); $off += $imgPart7.Length
    [Array]::Copy($msgBytes, 0, $imageBytes, $off, $msgBytes.Length)

    $headerBytes = New-Object byte[] 28
    $magic = [System.Text.Encoding]::ASCII.GetBytes("R3BIN01")
    [Array]::Copy($magic, 0, $headerBytes, 0, $magic.Length)
    $headerBytes[7] = 0
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $headerBytes, 8, 4)   # version
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]0), 0, $headerBytes, 12, 4)  # entry_offset
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]28), 0, $headerBytes, 16, 4) # image_offset
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]$imageBytes.Length), 0, $headerBytes, 20, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $headerBytes, 24, 4)  # stack_pages

    $outDir = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        [System.IO.Directory]::CreateDirectory($outDir) | Out-Null
    }
    $fs = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $fs.Write($headerBytes, 0, $headerBytes.Length)
        $fs.Write($imageBytes, 0, $imageBytes.Length)
    }
    finally {
        $fs.Dispose()
    }
}

if (-Not $NoRun) {
    Write-Host "Stopping existing QEMU processes..." -ForegroundColor Yellow
    Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue
}

# 1. コンパイラとリンカのパス（LLVM）
$clangPath = "C:\Program Files\LLVM\bin\clang.exe"
$lldLinkPath = "C:\Program Files\LLVM\bin\lld-link.exe"
Test-ToolExists -ToolLabel "clang" -ToolPath $clangPath
Test-ToolExists -ToolLabel "lld-link" -ToolPath $lldLinkPath

# OVFMF.fd (UEFI BIOS ROM) は本来QEMU付属のものか、EDK2のビルド済みイメージが必要
# ここではQEMUパッケージなどに一部付属している想定（もしくは警告無視）で起動テストします

Write-Host "Compiling main.c -> main.efi..." -ForegroundColor Cyan

# 2. Clangによるコンパイル (オブジェクトファイル生成)
# ターゲット: Windows環境のx86_64ターゲットを指定。UEFIは標準ライブラリを持たない(-ffreestanding)
& $clangPath -target x86_64-pc-win32-coff -mno-red-zone -fno-stack-protector -fshort-wchar -Wall -I boot -c boot/main.c -o main.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compile Error!" -ForegroundColor Red
    exit 1
}

# 3. LLD-LINKによるリンク (UEFIアプリケーション .efi を生成)
# /subsystem:efi_application が EFI_APPLICATION の意味
# /entry:efi_main がエントリポイント指定
& $lldLinkPath /subsystem:efi_application /entry:efi_main /out:main.efi main.o

if ($LASTEXITCODE -ne 0) {
    Write-Host "Link Error!" -ForegroundColor Red
    exit 1
}

Write-Host "Compiling kernel.c -> kernel.elf..." -ForegroundColor Cyan

# 4. カーネル本体とフォントのコンパイルとリンク (ELF形式)
$ldLldPath = "C:\Program Files\LLVM\bin\ld.lld.exe"
Test-ToolExists -ToolLabel "ld.lld" -ToolPath $ldLldPath
$commonKernelIncludes = @(
    "-I", "boot",
    "-I", "kernel",
    "-I", "kernel/arch/x86_64",
    "-I", "kernel/memory",
    "-I", "kernel/graphics"
)

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fshort-wchar -Wall @commonKernelIncludes -c kernel/graphics/font.c -o font.o
if ($LASTEXITCODE -ne 0) { Write-Host "Font Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/console.cpp -o console.o
if ($LASTEXITCODE -ne 0) { Write-Host "Console Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/mouse.cpp -o mouse.o
if ($LASTEXITCODE -ne 0) { Write-Host "Mouse Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/interrupt.cpp -o interrupt.o
if ($LASTEXITCODE -ne 0) { Write-Host "Interrupt Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/pic.cpp -o pic.o
if ($LASTEXITCODE -ne 0) { Write-Host "PIC Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/ps2.cpp -o ps2.o
if ($LASTEXITCODE -ne 0) { Write-Host "PS2 Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/pci.cpp -o pci.o
if ($LASTEXITCODE -ne 0) { Write-Host "PCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/usb/xhci.cpp -o xhci.o
if ($LASTEXITCODE -ne 0) { Write-Host "xHCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/commands.cpp -o shell_commands.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Commands Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/text.cpp -o shell_text.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Text Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_dispatch.cpp -o shell_cmd_dispatch.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Dispatch Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_core.cpp -o shell_cmd_core.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell Core Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_fs.cpp -o shell_cmd_fs.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell FS Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/cmd_xhci.cpp -o shell_cmd_xhci.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell xHCI Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/shell/tab_completion.cpp -o shell_tab_completion.o
if ($LASTEXITCODE -ne 0) { Write-Host "Shell TabCompletion Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/syscall/syscall.cpp -o syscall_core.o
if ($LASTEXITCODE -ne 0) { Write-Host "Syscall Core Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/proc/process.cpp -o proc_process.o
if ($LASTEXITCODE -ne 0) { Write-Host "Process Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/proc/scheduler.cpp -o proc_scheduler.o
if ($LASTEXITCODE -ne 0) { Write-Host "Scheduler Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_event.cpp -o input_key_event.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyEvent Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_layout.cpp -o input_key_layout.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyLayout Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/hid_keyboard.cpp -o input_hid_keyboard.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input HID Keyboard Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/history.cpp -o input_history.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input History Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_ops.cpp -o input_line_ops.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineOps Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/selection.cpp -o input_selection.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Selection Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_render.cpp -o input_line_render.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineRender Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/line_editor.cpp -o input_line_editor.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input LineEditor Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_logic.cpp -o input_ime_logic.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Logic Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_engine.cpp -o input_ime_engine.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Engine Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/ime_session.cpp -o input_ime_session.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input IME Session Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_flow.cpp -o input_key_flow.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyFlow Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_handler.cpp -o input_key_handler.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyHandler Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/key_handler_exec.cpp -o input_key_handler_exec.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input KeyHandler Exec Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/runtime_input_flow.cpp -o input_runtime_input_flow.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Runtime Flow Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/input/input_runtime_bridge.cpp -o input_runtime_bridge.o
if ($LASTEXITCODE -ne 0) { Write-Host "Input Runtime Bridge Compile Error!" -ForegroundColor Red; exit 1 }

# 割り込みハンドラはSSEレジスタを使わないように -mgeneral-regs-only を指定する
& $clangPath -target x86_64-elf -mno-red-zone -mgeneral-regs-only -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/interrupt_handler.cpp -o interrupt_handler.o
if ($LASTEXITCODE -ne 0) { Write-Host "Interrupt Handler Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/memory/memory.cpp -o memory.o
if ($LASTEXITCODE -ne 0) { Write-Host "Memory Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/paging.cpp -o paging.o
if ($LASTEXITCODE -ne 0) { Write-Host "Paging Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/apic.cpp -o apic.o
if ($LASTEXITCODE -ne 0) { Write-Host "APIC Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/timer.cpp -o timer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Timer Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/arch/x86_64/syscall_entry.cpp -o syscall_entry.o
if ($LASTEXITCODE -ne 0) { Write-Host "Syscall Entry Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/window.cpp -o window.o
if ($LASTEXITCODE -ne 0) { Write-Host "Window Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/graphics/layer.cpp -o layer.o
if ($LASTEXITCODE -ne 0) { Write-Host "Layer Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/ui/system_monitor.cpp -o ui_system_monitor.o
if ($LASTEXITCODE -ne 0) { Write-Host "UI System Monitor Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/user/ring3.cpp -o user_ring3.o
if ($LASTEXITCODE -ne 0) { Write-Host "User Ring3 Compile Error!" -ForegroundColor Red; exit 1 }

& $clangPath -target x86_64-elf -mno-red-zone -fno-stack-protector -fno-exceptions -fno-rtti -std=c++17 -Wall @commonKernelIncludes -c kernel/kernel.cpp -o kernel.o
if ($LASTEXITCODE -ne 0) { Write-Host "Kernel Compile Error!" -ForegroundColor Red; exit 1 }

# ELFファイルとしてリンク
& $ldLldPath -m elf_x86_64 -z norelro --image-base 0x100000 --entry KernelMain -o kernel.elf kernel.o console.o mouse.o interrupt.o pic.o ps2.o pci.o xhci.o shell_commands.o shell_text.o shell_cmd_dispatch.o shell_cmd_core.o shell_cmd_fs.o shell_cmd_xhci.o shell_tab_completion.o syscall_core.o proc_process.o proc_scheduler.o input_key_event.o input_key_layout.o input_hid_keyboard.o input_history.o input_line_ops.o input_selection.o input_line_render.o input_line_editor.o input_ime_logic.o input_ime_engine.o input_ime_session.o input_key_flow.o input_key_handler.o input_key_handler_exec.o input_runtime_input_flow.o input_runtime_bridge.o interrupt_handler.o font.o memory.o paging.o apic.o timer.o syscall_entry.o window.o layer.o ui_system_monitor.o user_ring3.o
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
    Write-HostClipboardSnapshot -OutputPath (Join-Path $projectRoot "disk\host.clip")
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $projectRoot "tools\generate_r3bin_samples.ps1") -OutputDir (Join-Path $projectRoot "disk")
    if ($LASTEXITCODE -ne 0) {
        throw "generate_r3bin_samples.ps1 failed"
    }
}
catch {
    Write-Host "Error: failed to copy build artifacts into disk/." -ForegroundColor Red
    Write-Host ("Detail: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

Write-Host "Starting QEMU..." -ForegroundColor Cyan

$qemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe"
Test-ToolExists -ToolLabel "qemu-system-x86_64" -ToolPath $qemuPath

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
if ($enableUsbTablet) {
    $qemuArgs += @("-device", "qemu-xhci,msi=off", "-device", "usb-tablet")
}
$qemuArgs += @("-drive", "format=raw,file=fat:rw:disk")
if (-not $NoClipBridge) {
    $qemuArgs += @("-serial", "tcp:127.0.0.1:4545,server=on,wait=off")
}

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
    if ($enableUsbTablet) {
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
    $qemuProc = Start-Process -FilePath $qemuPath `
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

$bridgeProc = $null
if (-not $NoClipBridge) {
    $bridgeScript = Join-Path $projectRoot "tools\clip_bridge.ps1"
    if (Test-Path $bridgeScript) {
        try {
            $bridgeOut = Join-Path $projectRoot "qemu-clipbridge.stdout.log"
            $bridgeErr = Join-Path $projectRoot "qemu-clipbridge.stderr.log"
            if (Test-Path $bridgeOut) { Remove-Item $bridgeOut -Force -ErrorAction SilentlyContinue }
            if (Test-Path $bridgeErr) { Remove-Item $bridgeErr -Force -ErrorAction SilentlyContinue }
            $bridgeProc = Start-Process -FilePath "powershell.exe" `
                -ArgumentList @("-Sta", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $bridgeScript, "-TargetHost", "127.0.0.1", "-Port", "4545") `
                -WorkingDirectory $projectRoot `
                -RedirectStandardOutput $bridgeOut `
                -RedirectStandardError $bridgeErr `
                -PassThru
            Write-Host "Clipboard bridge started (COM1 tcp:4545)." -ForegroundColor DarkCyan
        }
        catch {
            Write-Host "Warning: failed to start clipboard bridge. continuing without realtime host clipboard." -ForegroundColor Yellow
            $bridgeProc = $null
        }
    }
}

$qemuProc = $null
try {
    $qemuProc = Start-Process -FilePath $qemuPath `
        -ArgumentList $qemuArgs `
        -WorkingDirectory $projectRoot `
        -PassThru
    Wait-Process -Id $qemuProc.Id
}
finally {
    if ($null -ne $bridgeProc -and -not $bridgeProc.HasExited) {
        Stop-Process -Id $bridgeProc.Id -Force -ErrorAction SilentlyContinue
    }
}
