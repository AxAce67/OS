# R3BIN01 Format

`ring3 runfile <path>` で実行するユーザーバイナリ形式。

## Header (packed, little-endian)

```c
struct Ring3UserBinHeader {
    uint8_t  magic[8];      // "R3BIN01\0"
    uint32_t version;       // 1
    uint32_t entry_offset;  // image内のエントリオフセット
    uint32_t image_offset;  // ファイル内 image 先頭オフセット
    uint32_t image_size;    // imageサイズ（最大 128 KiB）
    uint32_t stack_pages;   // 0なら1ページ。最大16ページ。
};
```

## Rules

- `magic` は `R3BIN01\0`
- `version` は `1`
- `entry_offset < image_size`
- `image_offset + image_size <= file_size`

`image` はそのままユーザー実行ページへコピーされ、`entry_offset` から実行される。

## Entry ABI (current)

- エントリ時のレジスタ:
  - `rdi = argc`
  - `rsi = argv` (`char**`, 最後は `NULL`)
  - `rdx = envp` (`char**`, 最後は `NULL`)
- `exec /app.r3bin a b` の場合、`argc=2`、`argv[0]="a"`、`argv[1]="b"`。
- `envp` にはシェル実行時の `KEY=VALUE` が渡される（例: `CWD=/`, `LAYOUT=jp`, `IME=off`, `set` で設定した変数）。

検証サンプル:
- `argc.r3bin`: `argc` を終了コードで返す
- `argv0head.r3bin`: `argv[0][0]` のASCIIコードを終了コードで返す
- `argv1head.r3bin`: `argv[1][0]` のASCIIコードを終了コードで返す（`argc < 2` は `0`）
- `env0head.r3bin`: `envp[0][0]` のASCIIコードを終了コードで返す（`envp` 空の場合は `0`）
- `env1head.r3bin`: `envp[1][0]` のASCIIコードを終了コードで返す（`envp[1]` が無ければ `0`）
- `env2head.r3bin`: `envp[2][0]` のASCIIコードを終了コードで返す（`envp[2]` が無ければ `0`）

## Exit Code Convention (PoC)

- `kExitToKernel` (`rax=4`) を呼ぶとき、`rdi` を終了コードとして扱う。
- `ring3 runfile` 実行後は `ring3.last_sysret` に表示される。
