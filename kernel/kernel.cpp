// kernel.c
#include <stdint.h>
#include "frame_buffer_config.h"
#include "font.h"

// ピクセルを一つ塗る関数（カーネル版）
void DrawPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= config->horizontal_resolution || y >= config->vertical_resolution) return;

    // config->pixels_per_scan_line はパディングを含んだ1行の論理的なピクセル数
    uint32_t index = (y * config->pixels_per_scan_line + x) * 4;
    
    if (config->pixel_format == kPixelRGBResv8BitPerColor) {
        config->frame_buffer[index]     = r; // Red
        config->frame_buffer[index + 1] = g; // Green
        config->frame_buffer[index + 2] = b; // Blue
    } else {
        config->frame_buffer[index]     = b; // Blue
        config->frame_buffer[index + 1] = g; // Green
        config->frame_buffer[index + 2] = r; // Red
    }
}

// ピクセルを一つ読み取る関数（背景保存用）
void ReadPixel(const struct FrameBufferConfig* config, uint32_t x, uint32_t y, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (x >= config->horizontal_resolution || y >= config->vertical_resolution) {
        r = g = b = 0;
        return;
    }

    uint32_t index = (y * config->pixels_per_scan_line + x) * 4;
    if (config->pixel_format == kPixelRGBResv8BitPerColor) {
        r = config->frame_buffer[index];
        g = config->frame_buffer[index + 1];
        b = config->frame_buffer[index + 2];
    } else {
        b = config->frame_buffer[index];
        g = config->frame_buffer[index + 1];
        r = config->frame_buffer[index + 2];
    }
}

// 文字を一つ描画する関数
void DrawChar(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, char c, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t* font_data = kFont[(uint8_t)c];
    for (int dy = 0; dy < 16; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
            if ((font_data[dy] << dx) & 0x80) { // 上位ビットから順にピクセルがONか確認
                DrawPixel(config, start_x + dx, start_y + dy, r, g, b);
            }
        }
    }
}

// 文字列を描画する関数
void DrawString(const struct FrameBufferConfig* config, uint32_t start_x, uint32_t start_y, const char* str, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t x = start_x;
    for (int i = 0; str[i] != '\0'; ++i) {
        DrawChar(config, x, start_y, str[i], r, g, b);
        x += 8; // 1文字進める
    }
}

#include "console.hpp"
#include "mouse.hpp"
#include "interrupt.hpp"
#include "interrupt_handler.hpp"
#include "pic.hpp"
#include "ps2.hpp"
#include "queue.hpp"
#include "boot_info.h"
#include "memory.hpp"
#include "paging.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "apic.hpp"
#include "timer.hpp"

namespace {
struct AllocationHeader {
    uint64_t magic;
    uint64_t num_pages;
};

const uint64_t kAllocationMagic = 0x4F53414C4C4F4341ULL;  // "OSALLOCA"
}

// 割り込みハンドラとやり取りするメッセージ定義（interrupt_handler.cpp側と同じ構造）
struct Message {
    enum class Type {
        kInterruptMouse,
        kInterruptKeyboard,
    } type;
    int dx, dy;
    uint8_t keycode;
};

namespace {
struct KeyboardState {
    bool left_shift;
    bool right_shift;
    bool caps_lock;
};

char KeycodeToAscii(uint8_t keycode, bool shift, bool caps_lock) {
    switch (keycode) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return (shift ^ caps_lock) ? 'Q' : 'q';
        case 0x11: return (shift ^ caps_lock) ? 'W' : 'w';
        case 0x12: return (shift ^ caps_lock) ? 'E' : 'e';
        case 0x13: return (shift ^ caps_lock) ? 'R' : 'r';
        case 0x14: return (shift ^ caps_lock) ? 'T' : 't';
        case 0x15: return (shift ^ caps_lock) ? 'Y' : 'y';
        case 0x16: return (shift ^ caps_lock) ? 'U' : 'u';
        case 0x17: return (shift ^ caps_lock) ? 'I' : 'i';
        case 0x18: return (shift ^ caps_lock) ? 'O' : 'o';
        case 0x19: return (shift ^ caps_lock) ? 'P' : 'p';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x1E: return (shift ^ caps_lock) ? 'A' : 'a';
        case 0x1F: return (shift ^ caps_lock) ? 'S' : 's';
        case 0x20: return (shift ^ caps_lock) ? 'D' : 'd';
        case 0x21: return (shift ^ caps_lock) ? 'F' : 'f';
        case 0x22: return (shift ^ caps_lock) ? 'G' : 'g';
        case 0x23: return (shift ^ caps_lock) ? 'H' : 'h';
        case 0x24: return (shift ^ caps_lock) ? 'J' : 'j';
        case 0x25: return (shift ^ caps_lock) ? 'K' : 'k';
        case 0x26: return (shift ^ caps_lock) ? 'L' : 'l';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x2C: return (shift ^ caps_lock) ? 'Z' : 'z';
        case 0x2D: return (shift ^ caps_lock) ? 'X' : 'x';
        case 0x2E: return (shift ^ caps_lock) ? 'C' : 'c';
        case 0x2F: return (shift ^ caps_lock) ? 'V' : 'v';
        case 0x30: return (shift ^ caps_lock) ? 'B' : 'b';
        case 0x31: return (shift ^ caps_lock) ? 'N' : 'n';
        case 0x32: return (shift ^ caps_lock) ? 'M' : 'm';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        default: return 0;
    }
}

bool HandleModifierKey(uint8_t scancode, KeyboardState& kb) {
    bool released = (scancode & 0x80) != 0;
    uint8_t keycode = scancode & 0x7F;
    switch (keycode) {
        case 0x2A:
            kb.left_shift = !released;
            return true;
        case 0x36:
            kb.right_shift = !released;
            return true;
        case 0x3A:
            if (!released) {
                kb.caps_lock = !kb.caps_lock;
            }
            return true;
        default:
            return false;
    }
}

bool IsShiftPressed(const KeyboardState& kb) {
    return kb.left_shift || kb.right_shift;
}
}

// C++標準ライブラリ（<new>）が存在しないため、配置new（Placement new）を自作する
void* operator new(size_t size, void* buf) {
    return buf;
}

// OSのメモリ管理機能を利用した、待望の真の「動的メモリ確保」
void* operator new(size_t size) {
    if (memory_manager == nullptr) {
        while (1) {
            __asm__ volatile("cli\n\thlt");
        }
    }

    const size_t total_size = size + sizeof(AllocationHeader);
    size_t num_pages = (total_size + kPageSize - 1) / kPageSize;
    uint64_t addr = memory_manager->Allocate(num_pages);
    if (addr == 0) {
        while (1) {
            __asm__ volatile("cli\n\thlt");
        }
    }

    auto* header = reinterpret_cast<AllocationHeader*>(addr);
    header->magic = kAllocationMagic;
    header->num_pages = num_pages;
    return reinterpret_cast<void*>(addr + sizeof(AllocationHeader));
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void* obj) noexcept {
    if (obj != nullptr && memory_manager != nullptr) {
        uint64_t obj_addr = reinterpret_cast<uint64_t>(obj);
        auto* header = reinterpret_cast<AllocationHeader*>(obj_addr - sizeof(AllocationHeader));
        if (header->magic == kAllocationMagic) {
            memory_manager->Free(reinterpret_cast<uint64_t>(header), header->num_pages);
        }
    }
}

void operator delete(void* obj, size_t size) noexcept {
    (void)size;
    operator delete(obj);
}

void operator delete[](void* obj) noexcept {
    operator delete(obj);
}

void operator delete[](void* obj, size_t size) noexcept {
    operator delete(obj, size);
}

// コンソールの実体を配置するバッファ（動的確保がまだできないため、配置newのような形で使う）
char console_buf[sizeof(Console)];
Console* console;

// マウスカーソルの実体を配置するバッファ
char mouse_cursor_buf[sizeof(MouseCursor)];
MouseCursor* mouse_cursor;

// メインキューとそのバッファ
char main_queue_buf[sizeof(ArrayQueue<Message, 256>)];
ArrayQueue<Message, 256>* main_queue;

// MemoryManagerの実体を配置するバッファ
char memory_manager_buf[sizeof(MemoryManager)];

// LayerManagerのグローバル変数
char layer_manager_buf[sizeof(LayerManager)];
LayerManager* layer_manager;

// カーネルの真のエントリポイント（UEFIシステムからは切り離されている）
// ブートローダー(main.efi)からポインタ経由で呼び出されるため、C言語の呼び出し規約を強制する
extern "C" void KernelMain(const struct BootInfo* boot_info) {
    const struct FrameBufferConfig* frame_buffer_config = boot_info->frame_buffer_config;

    // 0. IDT設定中に割り込みが来るとOSが吹き飛ぶ（トリプルフォールト）ため、必ず最初に割り込みを禁止(cli)する
    __asm__ volatile("cli");

    // 1. 最重要！割り込みとメモリ保護の基礎(GDT/IDT)を設定し、CPUをカーネルの支配下に置く
    InitializeGDT();
    InitializeIDT();

    // IDTに先ほど作った「マウス用割り込みハンドラ」を登録する
    InitializeInterruptHandlers();

    // 先にキューやメモリ関連を立ち上げる
    main_queue = new(main_queue_buf) ArrayQueue<Message, 256>();

    // 物理メモリの管理機構を立ち上げる
    memory_manager = new(memory_manager_buf) MemoryManager();
    memory_manager->Initialize(boot_info);

    // ★★★ 仮想メモリの初期化と適用 (ページング) ★★★
    // CR3が切り替わるとメモリのアドレス解釈が全て変わるため、即座にIDTも再ロードして
    // 割り込みハンドラへのアドレス解決がOSのページテーブル経由で正しく行われるようにする
    extern InterruptDescriptor idt[256];
    InitializePaging();
    LoadIDT(sizeof(idt) - 1, reinterpret_cast<uint64_t>(&idt[0]));

    // 2. GUI描画を総括する LayerManager の初期化
    layer_manager = new(layer_manager_buf) LayerManager(*frame_buffer_config);

    // 3. 背景用ウィンドウ（画面全体と同じサイズ）の作成とレイヤー登録
    Window* bg_window = new Window(
        frame_buffer_config->horizontal_resolution,
        frame_buffer_config->vertical_resolution
    );
    // 背景を黒で塗りつぶす
    bg_window->FillRectangle(0, 0, bg_window->Width(), bg_window->Height(), {0, 0, 0});
    
    Layer* bg_layer = layer_manager->NewLayer();
    bg_layer->SetWindow(bg_window).Move(0, 0);
    layer_manager->UpDown(bg_layer, 0); // 最背面(Z=0)に設定

    // 4. コンソールの初期化（描画先を背景ウィンドウのキャンバスに指定）
    console = new(console_buf) Console(bg_window, 
                                        255, 255, 255, // FG (White)
                                        0, 0, 0);      // BG (Black)
    console->Print("Initializing LayerManager...\n");

    // ★★★ 真の「動的メモリ確保 (new)」のテスト ★★★
    console->Print("Testing dynamic new : ");
    uint64_t* test_arr = new uint64_t[3];
    if (test_arr != nullptr) {
        test_arr[0] = 0xAA;
        test_arr[1] = 0xBB;
        console->Print("Success! (Addr: 0x");
        console->PrintHex(reinterpret_cast<uint64_t>(test_arr), 8);
        console->PrintLine(")");
        delete[] test_arr;
    } else {
        console->PrintLine("FAILED (Returned nullptr)");
    }

    console->PrintLine("Welcome to Native OS (C++ Edition)!");
    console->Print("Booting from ELF format...\n\n");
    console->Print("System is ready. Current MapKey is captured.\n\n");

    // マウスカーソルの初期化とレイヤーの登録
    console->Print("Drawing mouse cursor layer...\n");
    mouse_cursor = new(mouse_cursor_buf) MouseCursor(
        frame_buffer_config->horizontal_resolution / 2,
        frame_buffer_config->vertical_resolution / 2,
        layer_manager
    );

    // 最初に1度だけ画面全体を合成描画する
    layer_manager->Draw();

    // 5. 本格的なハードウェア割り込みを受け取るための環境構築
    console->Print("Initializing Interrupt Controller (PIC)...\n");
    InitializePIC(0x20); // IRQ0〜15 を IDTの 0x20〜0x2F (32〜47) に割り当てる

    console->Print("Initializing PS/2 Mouse...\n");
    InitializePS2Mouse();

    console->Print("Initializing Local APIC...\n");
    InitializeLocalAPIC();

    console->Print("Initializing LAPIC timer...\n");
    InitializeLAPICTimer();

    console->Print("Waiting for hardware interrupts (Keyboard/Mouse/LAPIC Timer)...\n");

    // 全ての初期化プロセスが終わった時点で、溜まったコンソール出力をVRAMへ全画面描画（反映）する
    layer_manager->Draw();

    // PICのマスクを解除して、実際に信号がCPUに飛んでくるようにする
    UnmaskIRQ(1);  // PS/2 Keyboard
    UnmaskIRQ(2);  // Slave PIC Cascade
    UnmaskIRQ(12); // PS/2 Mouse

    // sti: Set Interrupt Flag 命令を実行し、CPU全体として割り込みを「受ける」状態にする
    __asm__ volatile("sti");

    // OSのメインループ（イベントループ）
    uint64_t last_tick = 0;
    KeyboardState keyboard_state;
    keyboard_state.left_shift = false;
    keyboard_state.right_shift = false;
    keyboard_state.caps_lock = false;
    while (1) {
        // 処理すべきイベントがあるか、割り込みを禁止(cli)した上で安全にチェックする（競合対策）
        __asm__ volatile("cli");
        if (main_queue->Count() == 0) {
            uint64_t tick = CurrentTick();
            if (tick - last_tick >= 100) {
                last_tick = tick;
                __asm__ volatile("sti");
                console->Print("tick=");
                console->PrintDec(static_cast<int64_t>(tick));
                console->Print("\n");
                layer_manager->Draw();
                continue;
            }
            // キューが空ならば、割り込みを許可(sti)すると同時にCPUを休止(hlt)させる
            __asm__ volatile("sti\n\thlt");
            continue;
        }

        // キューにデータが入っていたら、メッセージを1つ取り出す
        Message msg;
        main_queue->Pop(msg);
        
        // 取り出し終わったら割り込みを再開する
        __asm__ volatile("sti");

        // 取り出したメッセージの種類ごとに重い処理（状態の更新）を行う
        switch (msg.type) {
            case Message::Type::kInterruptMouse:
                mouse_cursor->Move(msg.dx, msg.dy);
                break;
            case Message::Type::kInterruptKeyboard:
                if (HandleModifierKey(msg.keycode, keyboard_state)) {
                    break;
                }
                if ((msg.keycode & 0x80) == 0) {
                    char ch = KeycodeToAscii(msg.keycode & 0x7F,
                                             IsShiftPressed(keyboard_state),
                                             keyboard_state.caps_lock);
                    if (ch != 0) {
                        char text[2] = {ch, '\0'};
                        console->Print(text);
                    } else {
                        console->Print("[");
                        console->PrintHex(msg.keycode, 2);
                        console->Print("]");
                    }
                    layer_manager->Draw();
                }
                break;
            default:
                break;
        }

    }
}
