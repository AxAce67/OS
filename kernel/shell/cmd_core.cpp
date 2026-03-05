#include <stdint.h>
#include "console.hpp"
#include "memory.hpp"
#include "timer.hpp"
#include "arch/x86_64/interrupt_handler.hpp"
#include "arch/x86_64/interrupt.hpp"
#include "arch/x86_64/paging.hpp"
#include "boot_info.h"
#include "shell/context.hpp"
#include "shell/text.hpp"
#include "arch/x86_64/syscall_entry.hpp"
#include "syscall/syscall.hpp"
#include "user/ring3.hpp"

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

namespace {
struct ExecProcessEntry {
    bool used;
    uint32_t pid;
    bool running;
    int64_t exit_code;
    uint64_t start_tick;
    uint64_t end_tick;
    char path[96];
    char status[16];
};

ExecProcessEntry g_exec_processes[16];
uint32_t g_exec_next_pid = 1;
int g_exec_next_slot = 0;
constexpr int kExecMaxEnv = 24;

void InitExecProcessTableIfNeeded() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_exec_processes) / sizeof(g_exec_processes[0])); ++i) {
        g_exec_processes[i].used = false;
    }
    initialized = true;
}

ExecProcessEntry* BeginExecProcessRecord(const char* path) {
    InitExecProcessTableIfNeeded();
    ExecProcessEntry* e = &g_exec_processes[g_exec_next_slot];
    g_exec_next_slot = (g_exec_next_slot + 1) % static_cast<int>(sizeof(g_exec_processes) / sizeof(g_exec_processes[0]));
    e->used = true;
    e->pid = g_exec_next_pid++;
    e->running = true;
    e->exit_code = 0;
    e->start_tick = CurrentTick();
    e->end_tick = 0;
    CopyString(e->path, path, sizeof(e->path));
    CopyString(e->status, "running", sizeof(e->status));
    return e;
}

void EndExecProcessRecord(ExecProcessEntry* e, bool ok, int64_t exit_code) {
    if (e == nullptr) {
        return;
    }
    e->running = false;
    e->exit_code = exit_code;
    e->end_tick = CurrentTick();
    CopyString(e->status, ok ? "exited" : "failed", sizeof(e->status));
}

bool AppendExecEnv(char out[][128], const char** out_ptrs, int max_count, int* io_count,
                   const char* key, const char* value) {
    if (out == nullptr || out_ptrs == nullptr || io_count == nullptr || key == nullptr || value == nullptr) {
        return false;
    }
    int count = *io_count;
    if (count < 0 || count >= max_count) {
        return false;
    }
    char* dst = out[count];
    int di = 0;
    for (int i = 0; key[i] != '\0' && di + 1 < 128; ++i) {
        dst[di++] = key[i];
    }
    if (di + 1 >= 128) {
        return false;
    }
    dst[di++] = '=';
    for (int i = 0; value[i] != '\0' && di + 1 < 128; ++i) {
        dst[di++] = value[i];
    }
    dst[di] = '\0';
    out_ptrs[count] = dst;
    *io_count = count + 1;
    return true;
}

bool ParseExecEnvAssign(const char* token, char* out_key, int out_key_len, char* out_value, int out_value_len) {
    if (token == nullptr || out_key == nullptr || out_value == nullptr || out_key_len <= 1 || out_value_len <= 1) {
        return false;
    }
    int eq = -1;
    for (int i = 0; token[i] != '\0'; ++i) {
        if (token[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq <= 0) {
        return false;
    }
    if (eq >= out_key_len) {
        return false;
    }
    for (int i = 0; i < eq; ++i) {
        out_key[i] = token[i];
    }
    out_key[eq] = '\0';
    int vi = 0;
    for (int i = eq + 1; token[i] != '\0' && vi + 1 < out_value_len; ++i) {
        out_value[vi++] = token[i];
    }
    out_value[vi] = '\0';
    return true;
}

int FindExecEnvIndex(const char* const* env_ptrs, int envc, const char* key) {
    if (env_ptrs == nullptr || key == nullptr) {
        return -1;
    }
    for (int i = 0; i < envc; ++i) {
        const char* e = env_ptrs[i];
        if (e == nullptr) {
            continue;
        }
        int k = 0;
        while (key[k] != '\0' && e[k] != '\0' && e[k] != '=') {
            if (key[k] != e[k]) {
                break;
            }
            ++k;
        }
        if (key[k] == '\0' && e[k] == '=') {
            return i;
        }
    }
    return -1;
}

bool UpsertExecEnv(char out[][128], const char** out_ptrs, int max_count, int* io_count,
                   const char* key, const char* value) {
    if (out == nullptr || out_ptrs == nullptr || io_count == nullptr) {
        return false;
    }
    const int idx = FindExecEnvIndex(out_ptrs, *io_count, key);
    if (idx >= 0) {
        int di = 0;
        for (int i = 0; key[i] != '\0' && di + 1 < 128; ++i) {
            out[idx][di++] = key[i];
        }
        if (di + 1 >= 128) {
            return false;
        }
        out[idx][di++] = '=';
        for (int i = 0; value[i] != '\0' && di + 1 < 128; ++i) {
            out[idx][di++] = value[i];
        }
        out[idx][di] = '\0';
        out_ptrs[idx] = out[idx];
        return true;
    }
    return AppendExecEnv(out, out_ptrs, max_count, io_count, key, value);
}
}  // namespace

bool ExecuteHelpCommand() {
    console->PrintLine("help: core  help clear tick time mem uptime echo reboot exec procs");
    console->PrintLine("help: fs1   pwd cd mkdir touch write append cp");
    console->PrintLine("help: fs2   rm rmdir mv find grep ls stat cat");
    console->PrintLine("help: misc  history clearhistory inputstat about");
    console->PrintLine("help: cfg   repeat layout ime(on/off/toggle/stat/save/import/export/resetlearn) set alias syscall ring3 xhciinfo xhciregs xhcistop xhcistart xhcireset xhciinit xhcienableslot xhciaddress xhciconfigep xhciintrin xhcihidpoll xhcihidstat xhciauto xhciautostart mouseabs usbports");
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
        console->PrintLine("syscall abi=1 methods=write,tick,version,getenv trap=int80");
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

    if (StrStartsWith(rest, "getenv ")) {
        const char* key = rest + 7;
        const uint64_t key_len = static_cast<uint64_t>(StrLength(key));
        char value[128];
        const int64_t ret = syscall::Dispatch(
            static_cast<uint64_t>(syscall::Number::kGetEnv),
            reinterpret_cast<uint64_t>(key),
            key_len,
            reinterpret_cast<uint64_t>(value),
            static_cast<uint64_t>(sizeof(value)));
        console->Print("syscall getenv -> ");
        console->PrintDec(ret);
        if (ret >= 0) {
            console->Print(" value=");
            console->PrintLine(value);
        } else {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")\n");
        }
        return true;
    }

    if (StrEqual(rest, "trap tick")) {
        const int64_t ret = InvokeSyscallInt80(static_cast<uint64_t>(syscall::Number::kCurrentTick), 0, 0, 0, 0);
        console->Print("syscall trap tick -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrEqual(rest, "trap version")) {
        const int64_t ret = InvokeSyscallInt80(static_cast<uint64_t>(syscall::Number::kAbiVersion), 0, 0, 0, 0);
        console->Print("syscall trap version -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrStartsWith(rest, "trap write ")) {
        const char* text = rest + 11;
        const int64_t ret = InvokeSyscallInt80(
            static_cast<uint64_t>(syscall::Number::kWriteText),
            reinterpret_cast<uint64_t>(text),
            static_cast<uint64_t>(StrLength(text)),
            0,
            0);
        console->Print("\nsyscall trap write -> ");
        console->PrintDec(ret);
        console->Print("\n");
        return true;
    }

    if (StrEqual(rest, "trap invalid")) {
        const int64_t ret = InvokeSyscallInt80(0xFFFF, 0, 0, 0, 0);
        console->Print("syscall trap invalid -> ");
        console->PrintDec(ret);
        console->Print(" (");
        console->Print(syscall::ErrorName(ret));
        console->Print(")\n");
        return true;
    }

    if (StrEqual(rest, "trap badptr")) {
        const int64_t ret = InvokeSyscallInt80(
            static_cast<uint64_t>(syscall::Number::kWriteText),
            0x8,
            16,
            0,
            0);
        console->Print("syscall trap badptr -> ");
        console->PrintDec(ret);
        console->Print(" (");
        console->Print(syscall::ErrorName(ret));
        console->Print(")\n");
        return true;
    }

    if (StrStartsWith(rest, "trap getenv ")) {
        const char* key = rest + 12;
        const uint64_t key_len = static_cast<uint64_t>(StrLength(key));
        char value[128];
        const int64_t ret = InvokeSyscallInt80(
            static_cast<uint64_t>(syscall::Number::kGetEnv),
            reinterpret_cast<uint64_t>(key),
            key_len,
            reinterpret_cast<uint64_t>(value),
            static_cast<uint64_t>(sizeof(value)));
        console->Print("syscall trap getenv -> ");
        console->PrintDec(ret);
        if (ret >= 0) {
            console->Print(" value=");
            console->PrintLine(value);
        } else {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")\n");
        }
        return true;
    }

    console->PrintLine("usage: syscall [stat|tick|version|write <text>|getenv <key>|invalid|trap tick|trap version|trap write <text>|trap getenv <key>|trap invalid|trap badptr]");
    return true;
}

bool ExecuteRing3Command(const char* rest) {
    if (StrEqual(rest, "prep")) {
        if (usermode::PrepareRing3Stack(1)) {
            console->PrintLine("ring3.prep=ok");
        } else {
            console->PrintLine("ring3.prep=failed");
        }
    } else if (StrEqual(rest, "run")) {
        if (usermode::RunRing3Hello()) {
            console->PrintLine("ring3.run=ok");
            const int64_t ret = usermode::GetLastRing3SyscallReturn();
            console->Print("ring3.last_sysret=");
            console->PrintDec(ret);
            console->Print("\n");
        } else {
            console->PrintLine("ring3.run=failed");
        }
    } else if (StrEqual(rest, "runfault")) {
        if (usermode::RunRing3BadPtrTest()) {
            console->PrintLine("ring3.runfault=ok");
            const int64_t ret = usermode::GetLastRing3SyscallReturn();
            console->Print("ring3.last_sysret=");
            console->PrintDec(ret);
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")\n");
        } else {
            console->PrintLine("ring3.runfault=failed");
        }
    } else if (StrStartsWith(rest, "runfile ")) {
        const char* path = rest + 8;
        const BootFileEntry* file = FindBootFileByPath(g_cwd, path);
        if (file == nullptr) {
            console->Print("ring3.runfile=missing: ");
            console->PrintLine(path);
        } else if (usermode::RunRing3BinaryFromBuffer(file->data, file->size)) {
            console->Print("ring3.runfile=ok: ");
            console->PrintLine(path);
            const int64_t ret = usermode::GetLastRing3SyscallReturn();
            console->Print("ring3.last_sysret=");
            console->PrintDec(ret);
            if (ret < 0) {
                console->Print(" (");
                console->Print(syscall::ErrorName(ret));
                console->Print(")");
            }
            console->Print("\n");
        } else {
            console->Print("ring3.runfile=failed: ");
            console->Print(path);
            console->Print(" (");
            console->Print(usermode::GetLastRing3Error());
            console->Print(")\n");
        }
    } else if (StrEqual(rest, "reset")) {
        usermode::ResetRing3Stack();
        console->PrintLine("ring3.reset=ok");
    } else if (rest[0] != '\0' && !StrEqual(rest, "stat")) {
        console->PrintLine("usage: ring3 [stat|prep|run|runfault|runfile <path>|reset]");
        return true;
    }

    usermode::Ring3PrepState state{};
    usermode::GetRing3PrepState(&state);
    console->Print("ring3.segments=");
    console->PrintLine(IsUserModeSegmentsReady() ? "ready" : "not-ready");
    console->Print("ring3.tss.rsp0=0x");
    console->PrintHex(GetKernelTSSStack(), 16);
    console->Print("\n");
    console->Print("ring3.paging.user=");
    console->PrintLine(AreUserModeMappingsReady() ? "ready" : "not-ready");
    console->Print("ring3.stack.ready=");
    console->PrintLine(state.ready ? "yes" : "no");
    if (state.ready) {
        console->Print("ring3.code.base=0x");
        console->PrintHex(state.code_base, 16);
        console->Print("\n");
        console->Print("ring3.code.top=0x");
        console->PrintHex(state.code_top, 16);
        console->Print("\n");
        console->Print("ring3.code.pages=");
        console->PrintDec(static_cast<int64_t>(state.code_pages));
        console->Print("\n");
        console->Print("ring3.stack.base=0x");
        console->PrintHex(state.stack_base, 16);
        console->Print("\n");
        console->Print("ring3.stack.top=0x");
        console->PrintHex(state.stack_top, 16);
        console->Print("\n");
        console->Print("ring3.stack.pages=");
        console->PrintDec(static_cast<int64_t>(state.stack_pages));
        console->Print("\n");
    }
    console->PrintLine("ring3.next=enter ring3 hello (WIP)");
    return true;
}

bool ExecuteExecCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char path[96] = {};
    char args[16][64];
    const char* arg_ptrs[16];
    int argc = 0;
    char env_opt_keys[kExecMaxEnv][32];
    char env_opt_values[kExecMaxEnv][96];
    int env_opt_count = 0;

    while (true) {
        char tok[64];
        if (!NextToken(command, &pos, tok, sizeof(tok))) {
            break;
        }
        if (StrEqual(tok, "--env")) {
            char assign[96];
            if (!NextToken(command, &pos, assign, sizeof(assign))) {
                console->PrintLine("exec: --env requires KEY=VALUE");
                return true;
            }
            if (env_opt_count >= kExecMaxEnv) {
                console->PrintLine("exec: too many --env options");
                return true;
            }
            if (!ParseExecEnvAssign(assign, env_opt_keys[env_opt_count], sizeof(env_opt_keys[env_opt_count]),
                                    env_opt_values[env_opt_count], sizeof(env_opt_values[env_opt_count]))) {
                console->PrintLine("exec: invalid --env format (use KEY=VALUE)");
                return true;
            }
            ++env_opt_count;
            continue;
        }
        if (path[0] == '\0') {
            CopyString(path, tok, sizeof(path));
            continue;
        }
        if (argc >= static_cast<int>(sizeof(args) / sizeof(args[0]))) {
            console->PrintLine("exec: too many arguments");
            return true;
        }
        CopyString(args[argc], tok, sizeof(args[argc]));
        arg_ptrs[argc] = args[argc];
        ++argc;
    }
    if (path[0] == '\0') {
        console->PrintLine("exec: path required");
        console->PrintLine("usage: exec [--env KEY=VALUE ...] <bootfs-path> [args...]");
        return true;
    }

    char envs[kExecMaxEnv][128];
    const char* env_ptrs[kExecMaxEnv];
    int envc = 0;
    if (!UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, "CWD", g_cwd) ||
        !UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, "LAYOUT", g_jp_layout ? "jp" : "us") ||
        !UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, "IME", g_ime_enabled ? "on" : "off")) {
        console->PrintLine("exec: env overflow");
        return true;
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])); ++i) {
        if (g_vars[i].key[0] == '\0') {
            continue;
        }
        if (!UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, g_vars[i].key, g_vars[i].value)) {
            console->PrintLine("exec: env full (some vars dropped)");
            break;
        }
    }
    for (int i = 0; i < env_opt_count; ++i) {
        if (!UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, env_opt_keys[i], env_opt_values[i])) {
            console->PrintLine("exec: env full (some --env dropped)");
            break;
        }
    }

    const BootFileEntry* file = FindBootFileByPath(g_cwd, path);
    ExecProcessEntry* proc = BeginExecProcessRecord(path);
    if (file == nullptr) {
        console->Print("exec: not found: ");
        console->PrintLine(path);
        EndExecProcessRecord(proc, false, -1);
        return true;
    }
    if (usermode::RunRing3BinaryFromBufferWithArgsEnv(file->data, file->size, arg_ptrs, argc, env_ptrs, envc)) {
        console->Print("exec: ok: ");
        console->PrintLine(path);
        const int64_t ret = usermode::GetLastRing3SyscallReturn();
        console->Print("exec.ret=");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        EndExecProcessRecord(proc, true, ret);
    } else {
        console->Print("exec: failed: ");
        console->Print(path);
        console->Print(" (");
        console->Print(usermode::GetLastRing3Error());
        console->Print(")\n");
        EndExecProcessRecord(proc, false, -1);
    }
    return true;
}

bool ExecuteProcsCommand() {
    InitExecProcessTableIfNeeded();
    console->PrintLine("pid status  exit    start end   path");
    bool any = false;
    for (int n = 0; n < static_cast<int>(sizeof(g_exec_processes) / sizeof(g_exec_processes[0])); ++n) {
        const int idx =
            (g_exec_next_slot - 1 - n + static_cast<int>(sizeof(g_exec_processes) / sizeof(g_exec_processes[0]))) %
            static_cast<int>(sizeof(g_exec_processes) / sizeof(g_exec_processes[0]));
        const ExecProcessEntry* e = &g_exec_processes[idx];
        if (!e->used) {
            continue;
        }
        any = true;
        console->PrintDec(static_cast<int64_t>(e->pid));
        console->Print(" ");
        console->Print(e->status);
        console->Print(" ");
        console->PrintDec(e->exit_code);
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(e->start_tick));
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(e->end_tick));
        console->Print(" ");
        console->PrintLine(e->path);
    }
    if (!any) {
        console->PrintLine("(no exec records)");
    }
    return true;
}

