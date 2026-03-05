param(
    [string]$OutputDir = "."
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-R3BinFile {
    param(
        [string]$Path,
        [byte[]]$Image,
        [uint32]$EntryOffset = 0,
        [uint32]$StackPages = 1
    )
    $header = New-Object byte[] 28
    $magic = [System.Text.Encoding]::ASCII.GetBytes("R3BIN01")
    [Array]::Copy($magic, 0, $header, 0, $magic.Length)
    $header[7] = 0
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $header, 8, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]$EntryOffset), 0, $header, 12, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]28), 0, $header, 16, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]$Image.Length), 0, $header, 20, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([uint32]$StackPages), 0, $header, 24, 4)

    $bytes = New-Object byte[] ($header.Length + $Image.Length)
    [Array]::Copy($header, 0, $bytes, 0, $header.Length)
    [Array]::Copy($Image, 0, $bytes, $header.Length, $Image.Length)
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function New-ImageHello {
    $msg = [System.Text.Encoding]::ASCII.GetBytes("[ring3-file] hello from runfile`n")
    $msgLen = [uint64]$msg.Length

    $parts = @(
        [byte[]](0x48,0xB8,0x01,0,0,0,0,0,0,0),          # mov rax,1
        [byte[]](0x48,0x8D,0x3D,0x2C,0,0,0),             # lea rdi,[rip+44]
        [byte[]](0x48,0xBE),                             # mov rsi,imm64
        [System.BitConverter]::GetBytes($msgLen),
        [byte[]](0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80), # xor/xor/int80
        [byte[]](0x48,0x89,0xC7),                        # mov rdi,rax (exit code)
        [byte[]](0x48,0xB8,0x04,0,0,0,0,0,0,0),          # mov rax,4
        [byte[]](0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE),
        $msg
    )
    $len = 0
    foreach ($p in $parts) { $len += $p.Length }
    $buf = New-Object byte[] $len
    $o = 0
    foreach ($p in $parts) {
        [Array]::Copy($p, 0, $buf, $o, $p.Length)
        $o += $p.Length
    }
    return $buf
}

function New-ImageFault {
    return [byte[]](
        0x48,0xB8,0x01,0,0,0,0,0,0,0,      # mov rax,1
        0x48,0xBF,0x08,0,0,0,0,0,0,0,      # mov rdi,8
        0x48,0xBE,0x20,0,0,0,0,0,0,0,      # mov rsi,32
        0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,
        0x48,0x89,0xC7,                      # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,      # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageTick {
    return [byte[]](
        0x48,0xB8,0x02,0,0,0,0,0,0,0,      # mov rax,2 (tick)
        0x48,0x31,0xFF,0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,
        0x48,0x89,0xC7,                      # mov rdi,rax (exit code)
        0x48,0xB8,0x04,0,0,0,0,0,0,0,      # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageArgc {
    return [byte[]](
        0x48,0x89,0xFF,                          # mov rdi,rdi (argc already in rdi; keep explicit)
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageArgv0Head {
    return [byte[]](
        0x48,0x85,0xFF,                          # test rdi,rdi
        0x74,0x0D,                               # je +0x0D (to zero path)
        0x48,0x8B,0x06,                          # mov rax,[rsi]      ; argv[0]
        0x48,0x85,0xC0,                          # test rax,rax
        0x74,0x05,                               # je +0x05
        0x0F,0xB6,0x38,                          # movzx edi,byte [rax]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

[System.IO.Directory]::CreateDirectory($OutputDir) | Out-Null
New-R3BinFile -Path (Join-Path $OutputDir "hello.r3bin") -Image (New-ImageHello)
New-R3BinFile -Path (Join-Path $OutputDir "fault.r3bin") -Image (New-ImageFault)
New-R3BinFile -Path (Join-Path $OutputDir "tick.r3bin") -Image (New-ImageTick)
New-R3BinFile -Path (Join-Path $OutputDir "argc.r3bin") -Image (New-ImageArgc)
New-R3BinFile -Path (Join-Path $OutputDir "argv0head.r3bin") -Image (New-ImageArgv0Head)
