#include <stdint.h>
#include "console.hpp"
#include "memory.hpp"
#include "timer.hpp"
#include "arch/x86_64/interrupt_handler.hpp"
#include "boot_info.h"
#include "shell/context.hpp"
#include "shell/text.hpp"
#include "syscall/syscall.hpp"

extern Console* console;
extern bool g_key_repeat_enabled;
extern bool g_jp_layout;
extern bool g_ime_enabled;
extern int g_ime_user_candidate_count;
extern bool g_boot_mouse_auto_enabled;
extern bool g_xhci_hid_decode_keyboard;
extern bool g_has_halfwidth_kana_font;
extern ShellPair g_vars[16];
extern ShellPair g_aliases[16];
extern char g_cwd[96];
extern ShellFile g_files[64];
extern uint8_t g_mouse_buttons_current;
extern uint64_t g_mouse_left_press_count;
extern uint64_t g_mouse_right_press_count;
extern uint64_t g_mouse_middle_press_count;
extern uint64_t g_keyboard_irq_count;
extern uint8_t g_keyboard_last_raw;
extern uint8_t g_keyboard_last_key;
extern bool g_keyboard_last_extended;
extern bool g_keyboard_last_released;

ShellPair* EnsurePair(ShellPair* pairs, int count, const char* key);
void PrintPairs(const char* label, ShellPair* pairs, int count);
void Reboot();
bool ResolveFilePath(const char* cwd, const char* input, char* out, int out_len);
const ShellFile* FindShellFileByPath(const char* cwd, const char* input_path);
const BootFileEntry* FindBootFileByPath(const char* cwd, const char* input_path);
ShellFile* FindShellFileByAbsPathMutable(const char* abs_path);
ShellFile* CreateShellFile(const char* abs_path);
int CountImeLearningEntries();
void ClearImeLearning();
int ImportImeLearningFromBuffer(const uint8_t* data, int size, bool clear_before);
int ExportImeLearningToBuffer(char* out, int out_len);

bool ExecuteHelpCommand() {
    console->PrintLine("help: core  help clear tick time mem uptime echo reboot");
    console->PrintLine("help: fs1   pwd cd mkdir touch write append cp");
    console->PrintLine("help: fs2   rm rmdir mv find grep ls stat cat");
    console->PrintLine("help: misc  history clearhistory inputstat about");
    console->PrintLine("help: cfg   repeat layout ime(on/off/toggle/stat/save/import/export/resetlearn) set alias syscall xhciinfo xhciregs xhcistop xhcistart xhcireset xhciinit xhcienableslot xhciaddress xhciconfigep xhciintrin xhcihidpoll xhcihidstat xhciauto xhciautostart mouseabs usbports");
    return true;
}

bool ExecuteAboutCommand() {
    console->PrintLine("Native OS shell");
    console->PrintLine("arch: x86_64 / mode: kernel");
    return true;
}

bool ExecuteRepeatCommand(const char* rest) {
    if (rest[0] == '\0') {
        console->Print("repeat=");
        console->PrintLine(g_key_repeat_enabled ? "on" : "off");
        return true;
    }
    if (StrEqual(rest, "on")) {
        g_key_repeat_enabled = true;
        console->PrintLine("repeat=on");
        return true;
    }
    if (StrEqual(rest, "off")) {
        g_key_repeat_enabled = false;
        console->PrintLine("repeat=off");
        return true;
    }
    return false;
}

bool ExecuteLayoutCommand(const char* rest) {
    if (rest[0] == '\0') {
        console->Print("layout=");
        console->PrintLine(g_jp_layout ? "jp" : "us");
        return true;
    }
    if (StrEqual(rest, "us")) {
        g_jp_layout = false;
        console->PrintLine("layout=us");
        return true;
    }
    if (StrEqual(rest, "jp")) {
        g_jp_layout = true;
        console->PrintLine("layout=jp");
        return true;
    }
    return false;
}

bool ExecuteImeCommand(const char* rest) {
    if (StrEqual(rest, "stat")) {
        console->Print("ime=");
        console->Print(g_ime_enabled ? "on" : "off");
        console->Print(" layout=");
        console->Print(g_jp_layout ? "jp" : "us");
        console->Print(" dic=");
        console->PrintDec(g_ime_user_candidate_count);
        console->Print(" learn=");
        console->PrintDec(CountImeLearningEntries());
        console->Print("\n");
        return true;
    }
    if (StrEqual(rest, "resetlearn")) {
        ClearImeLearning();
        console->PrintLine("ime.learn reset");
        return true;
    }
    if (StrEqual(rest, "export")) {
        char out[2048];
        ExportImeLearningToBuffer(out, static_cast<int>(sizeof(out)));
        console->Print(out);
        return true;
    }
    if (StrStartsWith(rest, "save")) {
        int pos = 0;
        char sub[16];
        NextToken(rest, &pos, sub, sizeof(sub)); // "save"
        char name[64];
        if (!NextToken(rest, &pos, name, sizeof(name))) {
            CopyString(name, "/ime.learn.out", sizeof(name));
        }
        char extra[8];
        if (NextToken(rest, &pos, extra, sizeof(extra))) {
            console->PrintLine("ime save: too many arguments");
            return true;
        }
        char resolved[96];
        if (!ResolveFilePath(g_cwd, name, resolved, sizeof(resolved))) {
            console->PrintLine("ime save: invalid path");
            return true;
        }
        if (FindBootFileByPath(g_cwd, name) != nullptr) {
            console->PrintLine("ime save: boot file is read-only");
            return true;
        }
        ShellFile* file = FindShellFileByAbsPathMutable(resolved);
        if (file == nullptr) {
            file = CreateShellFile(resolved);
        }
        if (file == nullptr) {
            console->PrintLine("ime save: file table full");
            return true;
        }
        char out[2048];
        const int written = ExportImeLearningToBuffer(out, static_cast<int>(sizeof(out)));
        int copy_len = written;
        if (copy_len > static_cast<int>(sizeof(file->data))) {
            copy_len = static_cast<int>(sizeof(file->data));
        }
        for (int i = 0; i < copy_len; ++i) {
            file->data[i] = static_cast<uint8_t>(out[i]);
        }
        file->size = static_cast<uint64_t>(copy_len);
        console->Print("ime save: ");
        console->Print(resolved);
        if (copy_len < written) {
            console->PrintLine(" (truncated)");
        } else {
            console->PrintLine(" (ok)");
        }
        return true;
    }
    if (StrStartsWith(rest, "import")) {
        int pos = 0;
        char sub[16];
        NextToken(rest, &pos, sub, sizeof(sub)); // "import"
        char name[64];
        const ShellFile* src_user = nullptr;
        const BootFileEntry* src_boot = nullptr;
        if (NextToken(rest, &pos, name, sizeof(name))) {
            src_user = FindShellFileByPath(g_cwd, name);
            if (src_user == nullptr) {
                src_boot = FindBootFileByPath(g_cwd, name);
            }
        } else {
            src_user = FindShellFileByPath("/", "/ime.learn");
            if (src_user == nullptr) {
                src_boot = FindBootFileByPath("/", "/ime.learn");
            }
        }
        if (src_user == nullptr && src_boot == nullptr) {
            console->PrintLine("ime import: source not found");
            return true;
        }
        int imported = 0;
        if (src_user != nullptr) {
            const int n = (src_user->size > 0x7FFFFFFFu) ? 0x7FFFFFFF : static_cast<int>(src_user->size);
            imported = ImportImeLearningFromBuffer(src_user->data, n, false);
        } else {
            const int n = (src_boot->size > 0x7FFFFFFFu) ? 0x7FFFFFFF : static_cast<int>(src_boot->size);
            imported = ImportImeLearningFromBuffer(src_boot->data, n, false);
        }
        console->Print("ime import: +");
        console->PrintDec(imported);
        console->Print(" entries, total=");
        console->PrintDec(CountImeLearningEntries());
        console->Print("\n");
        return true;
    }
    if (rest[0] == '\0') {
        console->Print("ime=");
        console->PrintLine(g_ime_enabled ? "on" : "off");
        return true;
    }
    if (StrEqual(rest, "toggle")) {
        g_ime_enabled = !g_ime_enabled;
        if (g_ime_enabled) {
            g_jp_layout = true;
        }
        console->Print("ime=");
        console->PrintLine(g_ime_enabled ? "on" : "off");
        return true;
    }
    if (StrEqual(rest, "on")) {
        g_ime_enabled = true;
        g_jp_layout = true;
        console->PrintLine("ime=on");
        return true;
    }
    if (StrEqual(rest, "off")) {
        g_ime_enabled = false;
        console->PrintLine("ime=off");
        return true;
    }
    return false;
}

bool ExecuteSetCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char key[32];
    if (!NextToken(command, &pos, key, sizeof(key))) {
        PrintPairs("vars:", g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])));
        console->Print("mouse.auto=");
        console->PrintLine(g_boot_mouse_auto_enabled ? "on" : "off");
        console->Print("hid.kbd=");
        console->PrintLine(g_xhci_hid_decode_keyboard ? "on" : "off");
        return true;
    }
    if (StrEqual(key, "mouse.auto")) {
        const char* v = RestOfLine(command, pos);
        if (v[0] == '\0') {
            console->Print("mouse.auto=");
            console->PrintLine(g_boot_mouse_auto_enabled ? "on" : "off");
            return true;
        }
        if (StrEqual(v, "on")) {
            g_boot_mouse_auto_enabled = true;
            console->PrintLine("mouse.auto=on");
            return true;
        }
        if (StrEqual(v, "off")) {
            g_boot_mouse_auto_enabled = false;
            console->PrintLine("mouse.auto=off");
            return true;
        }
        console->PrintLine("set mouse.auto: use on/off");
        return true;
    }
    if (StrEqual(key, "hid.kbd")) {
        const char* v = RestOfLine(command, pos);
        if (v[0] == '\0') {
            console->Print("hid.kbd=");
            console->PrintLine(g_xhci_hid_decode_keyboard ? "on" : "off");
            return true;
        }
        if (StrEqual(v, "on")) {
            g_xhci_hid_decode_keyboard = true;
            console->PrintLine("hid.kbd=on");
            return true;
        }
        if (StrEqual(v, "off")) {
            g_xhci_hid_decode_keyboard = false;
            console->PrintLine("hid.kbd=off");
            return true;
        }
        console->PrintLine("set hid.kbd: use on/off");
        return true;
    }
    ShellPair* p = EnsurePair(g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])), key);
    if (p == nullptr) {
        console->PrintLine("set: table full");
        return true;
    }
    CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
    console->Print("set ");
    console->Print(p->key);
    console->Print("=");
    console->PrintLine(p->value);
    return true;
}

bool ExecuteAliasCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char key[32];
    if (!NextToken(command, &pos, key, sizeof(key))) {
        PrintPairs("aliases:", g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])));
        return true;
    }
    ShellPair* p = EnsurePair(g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])), key);
    if (p == nullptr) {
        console->PrintLine("alias: table full");
        return true;
    }
    CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
    console->Print("alias ");
    console->Print(p->key);
    console->Print("=");
    console->PrintLine(p->value);
    return true;
}

bool ExecuteEchoCommand(const char* rest) {
    console->PrintLine(rest);
    return true;
}

bool ExecuteRebootCommand() {
    console->PrintLine("rebooting...");
    Reboot();
    return true;
}

bool ExecutePwdCommand() {
    console->PrintLine(g_cwd);
    return true;
}

bool ExecuteClearCommand() {
    console->Clear();
    return true;
}

bool ExecuteTickTimeCommand() {
    console->Print("ticks=");
    console->PrintDec(static_cast<int64_t>(CurrentTick()));
    console->Print("\n");
    return true;
}

bool ExecuteUptimeCommand() {
    console->Print("uptime_ticks=");
    console->PrintDec(static_cast<int64_t>(CurrentTick()));
    console->Print("\n");
    return true;
}

bool ExecuteMemCommand() {
    uint64_t free_pages = memory_manager->CountFreePages();
    uint64_t free_mib = (free_pages * kPageSize) / kMiB;
    console->Print("free_ram=");
    console->PrintDec(static_cast<int64_t>(free_mib));
    console->PrintLine(" MiB");
    return true;
}

bool ExecuteInputStatCommand() {
    console->Print("kbd_irq=");
    console->PrintDec(static_cast<int64_t>(g_keyboard_irq_count));
    console->Print(" raw=0x");
    console->PrintHex(g_keyboard_last_raw, 2);
    console->Print(" key=0x");
    console->PrintHex(g_keyboard_last_key, 2);
    console->Print(" ext=");
    console->Print(g_keyboard_last_extended ? "1" : "0");
    console->Print(" up=");
    console->Print(g_keyboard_last_released ? "1" : "0");
    console->Print(" ");
    console->Print("kbd_dropped=");
    console->PrintDec(static_cast<int64_t>(g_keyboard_dropped_events));
    console->Print(" mouse_dropped=");
    console->PrintDec(static_cast<int64_t>(g_mouse_dropped_events));
    console->Print(" btn=0x");
    console->PrintHex(g_mouse_buttons_current, 2);
    console->Print(" l=");
    console->PrintDec(static_cast<int64_t>(g_mouse_left_press_count));
    console->Print(" r=");
    console->PrintDec(static_cast<int64_t>(g_mouse_right_press_count));
    console->Print(" m=");
    console->PrintDec(static_cast<int64_t>(g_mouse_middle_press_count));
    console->Print(" layout=");
    console->Print(g_jp_layout ? "jp" : "us");
    console->Print(" ime=");
    console->Print(g_ime_enabled ? "on" : "off");
    console->Print(" ime.font=");
    console->Print(g_has_halfwidth_kana_font ? "halfkana" : "ascii");
    console->Print(" ime.dic=");
    console->PrintDec(g_ime_user_candidate_count);
    console->Print(" ime.learn=");
    console->PrintDec(CountImeLearningEntries());
    console->Print(" hid.kbd=");
    console->Print(g_xhci_hid_decode_keyboard ? "on" : "off");
    console->Print("\n");
    return true;
}

bool ExecuteSyscallCommand(const char* rest) {
    if (rest[0] == '\0' || StrEqual(rest, "stat")) {
        console->PrintLine("syscall abi=1 methods=write,tick,version");
        return true;
    }

    if (StrEqual(rest, "tick")) {
        const int64_t ret = syscall::Dispatch(static_cast<uint64_t>(syscall::Number::kCurrentTick), 0, 0, 0, 0);
        console->Print("syscall tick -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrEqual(rest, "version")) {
        const int64_t ret = syscall::Dispatch(static_cast<uint64_t>(syscall::Number::kAbiVersion), 0, 0, 0, 0);
        console->Print("syscall version -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrStartsWith(rest, "write ")) {
        const char* text = rest + 6;
        const int64_t ret = syscall::Dispatch(
            static_cast<uint64_t>(syscall::Number::kWriteText),
            reinterpret_cast<uint64_t>(text),
            static_cast<uint64_t>(StrLength(text)),
            0,
            0);
        console->Print("\nsyscall write -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrEqual(rest, "invalid")) {
        const int64_t ret = syscall::Dispatch(0xFFFF, 0, 0, 0, 0);
        console->Print("syscall invalid -> ");
        console->PrintDec(ret);
        console->Print(" (");
        console->Print(syscall::ErrorName(ret));
        console->Print(")\n");
        return true;
    }

    console->PrintLine("usage: syscall [stat|tick|version|write <text>|invalid]");
    return true;
}

