// main.c
#include "efi.h"
#include "frame_buffer_config.h"
#include "boot_info.h"

// GOPを探すためのGUID
EFI_GUID gEfiGraphicsOutputProtocolGuid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
EFI_GUID gEfiLoadedImageProtocolGuid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

// カーネルのエントリーポイントの型を定義
// ブートローダー(Windows ABI)からELFカーネル(System V ABI)へジャンプするため、
// コンパイラに System V ABI で引数（RDIレジスタなど）を渡すよう指示する
typedef void __attribute__((sysv_abi)) (*KernelEntryPoint)(const struct BootInfo*);

#define EFI_ERROR_BIT         ((EFI_STATUS)1ULL << 63)
#define EFI_BUFFER_TOO_SMALL  (EFI_ERROR_BIT | 5)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2)

static int IsEfiError(EFI_STATUS status) {
    return status != EFI_SUCCESS;
}

static void Char16ToAscii(const CHAR16* src, char* dst, UINTN dst_len) {
    if (dst_len == 0) {
        return;
    }
    UINTN i = 0;
    for (; i + 1 < dst_len && src[i] != 0; ++i) {
        CHAR16 ch = src[i];
        dst[i] = (ch <= 0x7F) ? (char)ch : '?';
    }
    dst[i] = '\0';
}

static void CopyAscii(char* dst, const char* src, UINTN dst_len) {
    if (dst_len == 0) {
        return;
    }
    UINTN i = 0;
    for (; i + 1 < dst_len && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static EFI_STATUS LoadBootFileSystem(
    EFI_FILE_PROTOCOL* root,
    EFI_SYSTEM_TABLE* SystemTable,
    struct BootFileSystem** out_boot_fs) {

    *out_boot_fs = NULL;
    struct BootFileSystem* boot_fs = NULL;
    EFI_STATUS status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, sizeof(struct BootFileSystem), (void**)&boot_fs);
    if (IsEfiError(status) || boot_fs == NULL) {
        return status;
    }
    SystemTable->BootServices->SetMem(boot_fs, sizeof(struct BootFileSystem), 0);

    if (root->SetPosition != NULL) {
        root->SetPosition(root, 0);
    }

    while (boot_fs->file_count < kMaxBootFiles) {
        uint8_t dir_info_buffer[512];
        UINTN dir_info_size = sizeof(dir_info_buffer);
        status = root->Read(root, &dir_info_size, dir_info_buffer);
        if (IsEfiError(status)) {
            break;
        }
        if (dir_info_size == 0) {
            break;
        }

        EFI_FILE_INFO* dir_info = (EFI_FILE_INFO*)dir_info_buffer;
        if (dir_info->FileName[0] == 0) {
            continue;
        }
        if ((dir_info->Attribute & EFI_FILE_DIRECTORY) != 0) {
            continue;
        }

        EFI_FILE_PROTOCOL* file = NULL;
        status = root->Open(root, &file, dir_info->FileName, EFI_FILE_MODE_READ, 0);
        if (IsEfiError(status) || file == NULL) {
            continue;
        }

        UINTN info_size = 0;
        status = file->GetInfo(file, (EFI_GUID*)&gEfiFileInfoGuid, &info_size, NULL);
        if (status != EFI_BUFFER_TOO_SMALL || info_size == 0) {
            file->Close(file);
            continue;
        }

        EFI_FILE_INFO* info = NULL;
        status = SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (void**)&info);
        if (IsEfiError(status) || info == NULL) {
            file->Close(file);
            continue;
        }
        status = file->GetInfo(file, (EFI_GUID*)&gEfiFileInfoGuid, &info_size, info);
        if (IsEfiError(status)) {
            SystemTable->BootServices->FreePool(info);
            file->Close(file);
            continue;
        }

        if (info->FileSize > kMaxBootFileDataSize) {
            SystemTable->BootServices->FreePool(info);
            file->Close(file);
            continue;
        }

        void* data = NULL;
        status = SystemTable->BootServices->AllocatePool(EfiLoaderData, info->FileSize, &data);
        if (IsEfiError(status) || data == NULL) {
            SystemTable->BootServices->FreePool(info);
            file->Close(file);
            continue;
        }

        UINTN read_size = info->FileSize;
        status = file->Read(file, &read_size, data);
        file->Close(file);
        if (IsEfiError(status) || read_size != info->FileSize) {
            SystemTable->BootServices->FreePool(data);
            SystemTable->BootServices->FreePool(info);
            continue;
        }

        UINTN idx = boot_fs->file_count;
        char ascii_name[kMaxBootFileNameLen];
        Char16ToAscii(info->FileName, ascii_name, sizeof(ascii_name));
        CopyAscii(boot_fs->files[idx].name, ascii_name, sizeof(boot_fs->files[idx].name));
        boot_fs->files[idx].size = info->FileSize;
        boot_fs->files[idx].data = (uint8_t*)data;
        boot_fs->file_count++;
        SystemTable->BootServices->FreePool(info);
    }

    *out_boot_fs = boot_fs;
    return EFI_SUCCESS;
}

static int IsSupportedPixelFormat(EFI_GRAPHICS_PIXEL_FORMAT format) {
    return format == PixelBlueGreenRedReserved8BitPerColor ||
           format == PixelRedGreenBlueReserved8BitPerColor;
}

static void SelectBestGraphicsMode(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop, EFI_SYSTEM_TABLE* SystemTable) {
    if (gop == NULL || gop->Mode == NULL || gop->QueryMode == NULL || gop->SetMode == NULL) {
        return;
    }

    UINT32 best_mode = gop->Mode->Mode;
    UINT64 best_pixels = 0;

    for (UINT32 mode = 0; mode < gop->Mode->MaxMode; ++mode) {
        UINTN info_size = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;
        EFI_STATUS status = gop->QueryMode(gop, mode, &info_size, &info);
        if (IsEfiError(status) || info == NULL) {
            continue;
        }

        if (IsSupportedPixelFormat(info->PixelFormat)) {
            UINT64 pixels = (UINT64)info->HorizontalResolution * (UINT64)info->VerticalResolution;
            if (pixels > best_pixels) {
                best_pixels = pixels;
                best_mode = mode;
            }
        }
        SystemTable->BootServices->FreePool(info);
    }

    if (best_mode != gop->Mode->Mode) {
        gop->SetMode(gop, best_mode);
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conOut = SystemTable->ConOut;
    conOut->ClearScreen(conOut);
    conOut->OutputString(conOut, L"Booting Native OS...\r\n");

    // 1. GOPの取得（前回成功したものと同じ）
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = SystemTable->BootServices->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);

    if (status != EFI_SUCCESS) {
        conOut->OutputString(conOut, L"Error: Failed to locate GOP.\r\n");
        while(1){}
    }

    SelectBestGraphicsMode(gop, SystemTable);

    // 2. カーネルに渡すための情報構造体を埋める
    struct FrameBufferConfig config;
    config.frame_buffer = (uint8_t*)gop->Mode->FrameBufferBase;
    config.horizontal_resolution = gop->Mode->Info->HorizontalResolution;
    config.vertical_resolution = gop->Mode->Info->VerticalResolution;
    config.pixels_per_scan_line = gop->Mode->Info->PixelsPerScanLine;
    if (gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        config.pixel_format = kPixelBGRResv8BitPerColor;
    } else if (gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        config.pixel_format = kPixelRGBResv8BitPerColor;
    } else {
        conOut->OutputString(conOut, L"Error: Unsupported GOP PixelFormat.\r\n");
        while (1) {}
    }

    // ----------- ここから「カーネルファイルの読み込みとジャンプ」 -----------
    // 3. 今起動しているこのプログラム(main.efi)がロードされた情報を取得する
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    status = SystemTable->BootServices->OpenProtocol(
        ImageHandle, &gEfiLoadedImageProtocolGuid, (void **)&loaded_image,
        ImageHandle, NULL, 0x00000001 // BY_HANDLE_PROTOCOL
    );
    if (IsEfiError(status) || loaded_image == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to open LoadedImage protocol.\r\n");
        while (1) {}
    }

    // 4. このプログラムが入っていたディスク（ファイルシステム）を開く
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    status = SystemTable->BootServices->OpenProtocol(
        loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&fs,
        ImageHandle, NULL, 0x00000001
    );
    if (IsEfiError(status) || fs == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to open SimpleFileSystem protocol.\r\n");
        while (1) {}
    }

    // 5. ディスクのルートディレクトリを開く
    EFI_FILE_PROTOCOL *root = NULL;
    status = fs->OpenVolume(fs, &root);
    if (IsEfiError(status) || root == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to open root volume.\r\n");
        while (1) {}
    }

    // 6. カーネルファイル（kernel.elf）をオープンする
    EFI_FILE_PROTOCOL *kernel_file = NULL;
    status = root->Open(root, &kernel_file, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (IsEfiError(status)) {
        conOut->OutputString(conOut, L"Error: Failed to open kernel.elf\r\n");
        while(1){}
    }

    // 7. ファイルサイズを調べる
    UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 32;
    uint8_t file_info_buffer[256];
    status = kernel_file->GetInfo(kernel_file, (EFI_GUID *)&gEfiFileInfoGuid, &file_info_size, file_info_buffer);
    if (IsEfiError(status)) {
        conOut->OutputString(conOut, L"Error: Failed to get kernel file info.\r\n");
        while (1) {}
    }
    EFI_FILE_INFO *file_info = (EFI_FILE_INFO *)file_info_buffer;

    // 8. カーネルを一時的に読み込むためのバッファを確保
    void *kernel_buffer = NULL;
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, file_info->FileSize, &kernel_buffer);
    if (IsEfiError(status) || kernel_buffer == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to allocate kernel buffer.\r\n");
        while (1) {}
    }

    // 9. ファイルの中身をメモリに読み込む
    UINTN kernel_size = file_info->FileSize;
    status = kernel_file->Read(kernel_file, &kernel_size, kernel_buffer);
    if (IsEfiError(status) || kernel_size != file_info->FileSize) {
        conOut->OutputString(conOut, L"Error: Failed to read full kernel image.\r\n");
        while (1) {}
    }
    kernel_file->Close(kernel_file);

    // 追加: ルートディレクトリのファイルをRAMへ読み込み、カーネル側の ls/cat 用に渡す
    struct BootFileSystem* boot_fs = NULL;
    status = LoadBootFileSystem(root, SystemTable, &boot_fs);
    if (IsEfiError(status) || boot_fs == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to build boot file system.\r\n");
        while (1) {}
    }

    conOut->OutputString(conOut, L"Kernel loaded. Analyzing ELF header...\r\n");

    // 11. ELFファイルの解析とメモリ配置 (ELF Loader)
    Elf64_Ehdr* elf_header = (Elf64_Ehdr*)kernel_buffer;

    // ELFヘッダのシグネチャチェック (0x7F 'E' 'L' 'F')
    if (elf_header->e_ident[0] != 0x7F || elf_header->e_ident[1] != 'E' ||
        elf_header->e_ident[2] != 'L'  || elf_header->e_ident[3] != 'F') {
        conOut->OutputString(conOut, L"Error: Invalid ELF header.\r\n");
        while(1){}
    }

    // LOADセグメントの展開先アドレス範囲を計算する
    Elf64_Addr kernel_first_addr = 0xffffffffffffffff;
    Elf64_Addr kernel_last_addr = 0;
    Elf64_Phdr* phdr = (Elf64_Phdr*)((uint8_t*)elf_header + elf_header->e_phoff);

    for (int i = 0; i < elf_header->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_vaddr < kernel_first_addr) kernel_first_addr = phdr[i].p_vaddr;
        if (phdr[i].p_vaddr + phdr[i].p_memsz > kernel_last_addr) kernel_last_addr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
    if (kernel_first_addr > kernel_last_addr) {
        conOut->OutputString(conOut, L"Error: No PT_LOAD segment found.\r\n");
        while (1) {}
    }

    // 必要なページ数を確保する (4KB = 0x1000 バイト単位)
    UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
    EFI_PHYSICAL_ADDRESS kernel_base_addr = kernel_first_addr;
    
    status = SystemTable->BootServices->AllocatePages(
        AllocateAddress,
        EfiLoaderData,  // 実行可能・データ領域として確保
        num_pages,
        &kernel_base_addr
    );

    if (IsEfiError(status)) {
        conOut->OutputString(conOut, L"Error: Failed to allocate pages for ELF segments.\r\n");
        while(1){}
    }

    // セグメントをメモリの正しい仮想アドレス(VMA)にコピーする
    for (int i = 0; i < elf_header->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;

        // ファイルからデータをコピー
        SystemTable->BootServices->CopyMem(
            (void*)phdr[i].p_vaddr,
            (void*)((uint8_t*)elf_header + phdr[i].p_offset),
            phdr[i].p_filesz
        );

        // ファイルサイズよりメモリサイズが大きい部分は0で埋める (BSSセクション等)
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            SystemTable->BootServices->SetMem(
                (void*)(phdr[i].p_vaddr + phdr[i].p_filesz),
                phdr[i].p_memsz - phdr[i].p_filesz,
                0
            );
        }
    }

    // エントリーポイントの取得
    KernelEntryPoint kernel_main = (KernelEntryPoint)elf_header->e_entry;

    conOut->OutputString(conOut, L"Kernel loaded successfully.\r\n");

    // 10. ブートサービスを終了させる（OSに完全に主導権を渡す）
    // ExitBootServicesを呼ぶ直前の最新のメモリマップの「キー(MapKey)」が必要
    UINTN memory_map_size = 0;
    EFI_MEMORY_DESCRIPTOR* memory_map = NULL;
    UINTN map_key, descriptor_size;
    UINT32 descriptor_version;

    // まず必要なバッファサイズを取得
    status = SystemTable->BootServices->GetMemoryMap(
        &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        conOut->OutputString(conOut, L"Error: Unexpected GetMemoryMap status.\r\n");
        while (1) {}
    }
    
    // 取得したサイズに少し余裕を持たせて（このAllocatePool自体で容量が変わるため）メモリ確保
    memory_map_size += descriptor_size * 8;
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, memory_map_size, (void**)&memory_map);
    if (IsEfiError(status) || memory_map == NULL) {
        conOut->OutputString(conOut, L"Error: Failed to allocate memory-map buffer.\r\n");
        while (1) {}
    }

    while (1) {
        UINTN current_map_size = memory_map_size;
        status = SystemTable->BootServices->GetMemoryMap(
            &current_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        if (IsEfiError(status)) {
            conOut->OutputString(conOut, L"Error: Failed to get memory map.\r\n");
            while (1) {}
        }

        status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        if (status == EFI_SUCCESS) {
            memory_map_size = current_map_size;
            break;
        }
        if (status != EFI_INVALID_PARAMETER) {
            while (1) {}
        }
    }
    // ------ これ以降、conOut->OutputString などのUEFI関数は一切呼び出し禁止！！ ------

    // 11. 最終構成情報を作成し、カーネルの起動！
    struct BootInfo boot_info;
    boot_info.frame_buffer_config = &config;
    boot_info.memory_map          = (uint8_t*)memory_map;
    boot_info.memory_map_size     = memory_map_size;
    boot_info.descriptor_size     = descriptor_size;
    boot_info.boot_fs             = boot_fs;

    kernel_main(&boot_info);

    // もしカーネルから戻ってきた場合（基本ありえない。しかも表示手段がない）
    while (1) {}

    return EFI_SUCCESS;
}
