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

function New-ImageArgv1Head {
    return [byte[]](
        0x48,0x83,0xFF,0x02,                     # cmp rdi,2
        0x7C,0x0E,                               # jl +0x0E (zero path)
        0x48,0x8B,0x46,0x08,                     # mov rax,[rsi+8] ; argv[1]
        0x48,0x85,0xC0,                          # test rax,rax
        0x74,0x05,                               # je +0x05
        0x0F,0xB6,0x38,                          # movzx edi,byte [rax]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageEnv0Head {
    return [byte[]](
        0x48,0x85,0xD2,                          # test rdx,rdx
        0x74,0x0D,                               # je +0x0D (to zero path)
        0x48,0x8B,0x02,                          # mov rax,[rdx]      ; envp[0]
        0x48,0x85,0xC0,                          # test rax,rax
        0x74,0x05,                               # je +0x05
        0x0F,0xB6,0x38,                          # movzx edi,byte [rax]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageEnv1Head {
    return [byte[]](
        0x48,0x85,0xD2,                          # test rdx,rdx
        0x74,0x0E,                               # je +0x0E (to zero path)
        0x48,0x8B,0x42,0x08,                     # mov rax,[rdx+8]    ; envp[1]
        0x48,0x85,0xC0,                          # test rax,rax
        0x74,0x05,                               # je +0x05
        0x0F,0xB6,0x38,                          # movzx edi,byte [rax]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageEnv2Head {
    return [byte[]](
        0x48,0x85,0xD2,                          # test rdx,rdx
        0x74,0x0E,                               # je +0x0E (to zero path)
        0x48,0x8B,0x42,0x10,                     # mov rax,[rdx+16]   ; envp[2]
        0x48,0x85,0xC0,                          # test rax,rax
        0x74,0x05,                               # je +0x05
        0x0F,0xB6,0x38,                          # movzx edi,byte [rax]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE
    )
}

function New-ImageGetenvCwd0 {
    return [byte[]](
        0x48,0xB8,0x05,0,0,0,0,0,0,0,          # mov rax,5 (getenv)
        0x48,0x8D,0x3D,0x45,0,0,0,             # lea rdi,[rip+0x45] -> "CWD"
        0x48,0xBE,0x03,0,0,0,0,0,0,0,          # mov rsi,3
        0x48,0x8D,0x15,0x37,0,0,0,             # lea rdx,[rip+0x37] -> buf
        0x48,0xB9,0x10,0,0,0,0,0,0,0,          # mov rcx,16
        0xCD,0x80,                               # int 0x80
        0x48,0x85,0xC0,                          # test rax,rax
        0x7E,0x06,                               # jle +0x06
        0x0F,0xB6,0x3D,0x1F,0,0,0,             # movzx edi,byte [rip+0x1F] -> buf[0]
        0xEB,0x03,                               # jmp +0x03
        0x48,0x31,0xFF,                          # xor rdi,rdi
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x43,0x57,0x44,                          # "CWD"
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0        # buf[16]
    )
}

function New-ImageSetenvOk {
    return [byte[]](
        0x48,0xB8,0x06,0,0,0,0,0,0,0,          # mov rax,6 (setenv)
        0x48,0x8D,0x3D,0x37,0,0,0,             # lea rdi,[rip+0x37] -> "TEST"
        0x48,0xBE,0x04,0,0,0,0,0,0,0,          # mov rsi,4
        0x48,0x8D,0x15,0x2A,0,0,0,             # lea rdx,[rip+0x2A] -> "Z"
        0x48,0xB9,0x01,0,0,0,0,0,0,0,          # mov rcx,1
        0xCD,0x80,                               # int 0x80
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x54,0x45,0x53,0x54,                     # "TEST"
        0x5A                                      # "Z"
    )
}

function New-ImageUnsetenvAfterSet {
    return [byte[]](
        0x48,0xB8,0x06,0,0,0,0,0,0,0,          # mov rax,6 (setenv)
        0x48,0x8D,0x3D,0x7D,0,0,0,             # lea rdi,[rip+0x7D] -> "TEST"
        0x48,0xBE,0x04,0,0,0,0,0,0,0,          # mov rsi,4
        0x48,0x8D,0x15,0x70,0,0,0,             # lea rdx,[rip+0x70] -> "Q"
        0x48,0xB9,0x01,0,0,0,0,0,0,0,          # mov rcx,1
        0xCD,0x80,                               # int 0x80
        0x48,0xB8,0x07,0,0,0,0,0,0,0,          # mov rax,7 (unsetenv)
        0x48,0x8D,0x3D,0x4F,0,0,0,             # lea rdi,[rip+0x4F] -> "TEST"
        0x48,0xBE,0x04,0,0,0,0,0,0,0,          # mov rsi,4
        0x48,0x31,0xD2,0x48,0x31,0xC9,          # xor rdx/rcx
        0xCD,0x80,                               # int 0x80
        0x48,0xB8,0x05,0,0,0,0,0,0,0,          # mov rax,5 (getenv)
        0x48,0x8D,0x3D,0x2C,0,0,0,             # lea rdi,[rip+0x2C] -> "TEST"
        0x48,0xBE,0x04,0,0,0,0,0,0,0,          # mov rsi,4
        0x48,0x31,0xD2,0x48,0x31,0xC9,          # xor rdx/rcx
        0xCD,0x80,                               # int 0x80 (expect -22)
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x54,0x45,0x53,0x54,                     # "TEST"
        0x51                                      # "Q"
    )
}

function New-ImageSetenvBadKey {
    return [byte[]](
        0x48,0xB8,0x06,0,0,0,0,0,0,0,          # mov rax,6 (setenv)
        0x48,0x8D,0x3D,0x37,0,0,0,             # lea rdi,[rip+0x37] -> key
        0x48,0xBE,0x00,0,0,0,0,0,0,0,          # mov rsi,0 (invalid key len)
        0x48,0x8D,0x15,0x27,0,0,0,             # lea rdx,[rip+0x27] -> value
        0x48,0xB9,0x01,0,0,0,0,0,0,0,          # mov rcx,1
        0xCD,0x80,                               # int 0x80
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x4B,                                    # 'K'
        0x56                                     # 'V'
    )
}

function New-ImageUnsetenvBadKey {
    return [byte[]](
        0x48,0xB8,0x07,0,0,0,0,0,0,0,          # mov rax,7 (unsetenv)
        0x48,0x8D,0x3D,0x2C,0,0,0,             # lea rdi,[rip+0x2C] -> key
        0x48,0xBE,0x00,0,0,0,0,0,0,0,          # mov rsi,0 (invalid key len)
        0x48,0x31,0xD2,0x48,0x31,0xC9,          # xor rdx/rcx
        0xCD,0x80,                               # int 0x80
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x4B                                     # 'K'
    )
}

function New-ImageGetenvLen {
    return [byte[]](
        0x48,0xB8,0x05,0,0,0,0,0,0,0,          # mov rax,5 (getenv)
        0x48,0x8D,0x3D,0x2C,0,0,0,             # lea rdi,[rip+0x2C] -> "LAYOUT"
        0x48,0xBE,0x06,0,0,0,0,0,0,0,          # mov rsi,6
        0x48,0x31,0xD2,                          # xor rdx,rdx
        0x48,0x31,0xC9,                          # xor rcx,rcx
        0xCD,0x80,                               # int 0x80
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x4C,0x41,0x59,0x4F,0x55,0x54           # "LAYOUT"
    )
}

function New-ImageGetenvBad {
    return [byte[]](
        0x48,0xB8,0x05,0,0,0,0,0,0,0,          # mov rax,5 (getenv)
        0x48,0x8D,0x3D,0x2C,0,0,0,             # lea rdi,[rip+0x2C] -> "NOPE"
        0x48,0xBE,0x04,0,0,0,0,0,0,0,          # mov rsi,4
        0x48,0x31,0xD2,                          # xor rdx,rdx
        0x48,0x31,0xC9,                          # xor rcx,rcx
        0xCD,0x80,                               # int 0x80
        0x48,0x89,0xC7,                          # mov rdi,rax
        0x48,0xB8,0x04,0,0,0,0,0,0,0,          # mov rax,4
        0x48,0x31,0xF6,0x48,0x31,0xD2,0x48,0x31,0xC9,0xCD,0x80,0xEB,0xFE,
        0x4E,0x4F,0x50,0x45                     # "NOPE"
    )
}

[System.IO.Directory]::CreateDirectory($OutputDir) | Out-Null
New-R3BinFile -Path (Join-Path $OutputDir "hello.r3bin") -Image (New-ImageHello)
New-R3BinFile -Path (Join-Path $OutputDir "fault.r3bin") -Image (New-ImageFault)
New-R3BinFile -Path (Join-Path $OutputDir "tick.r3bin") -Image (New-ImageTick)
New-R3BinFile -Path (Join-Path $OutputDir "argc.r3bin") -Image (New-ImageArgc)
New-R3BinFile -Path (Join-Path $OutputDir "argv0head.r3bin") -Image (New-ImageArgv0Head)
New-R3BinFile -Path (Join-Path $OutputDir "argv1head.r3bin") -Image (New-ImageArgv1Head)
New-R3BinFile -Path (Join-Path $OutputDir "env0head.r3bin") -Image (New-ImageEnv0Head)
New-R3BinFile -Path (Join-Path $OutputDir "env1head.r3bin") -Image (New-ImageEnv1Head)
New-R3BinFile -Path (Join-Path $OutputDir "env2head.r3bin") -Image (New-ImageEnv2Head)
New-R3BinFile -Path (Join-Path $OutputDir "getenvcwd0.r3bin") -Image (New-ImageGetenvCwd0)
New-R3BinFile -Path (Join-Path $OutputDir "getenvlen.r3bin") -Image (New-ImageGetenvLen)
New-R3BinFile -Path (Join-Path $OutputDir "getenvbad.r3bin") -Image (New-ImageGetenvBad)
New-R3BinFile -Path (Join-Path $OutputDir "setenvok.r3bin") -Image (New-ImageSetenvOk)
New-R3BinFile -Path (Join-Path $OutputDir "unsetenvbad.r3bin") -Image (New-ImageUnsetenvAfterSet)
New-R3BinFile -Path (Join-Path $OutputDir "setenvbadkey.r3bin") -Image (New-ImageSetenvBadKey)
New-R3BinFile -Path (Join-Path $OutputDir "unsetenvbadkey.r3bin") -Image (New-ImageUnsetenvBadKey)
