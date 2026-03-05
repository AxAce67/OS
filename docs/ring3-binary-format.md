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

## Exit Code Convention (PoC)

- `kExitToKernel` (`rax=4`) を呼ぶとき、`rdi` を終了コードとして扱う。
- `ring3 runfile` 実行後は `ring3.last_sysret` に表示される。
