#include <stdint.h>
#include "console.hpp"
#include "memory.hpp"
#include "timer.hpp"
#include "arch/x86_64/interrupt_handler.hpp"
#include "arch/x86_64/interrupt.hpp"
#include "arch/x86_64/paging.hpp"
#include "boot_info.h"
#include "proc/process.hpp"
#include "proc/scheduler.hpp"
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
extern uint64_t g_clip_rx_bytes;
extern uint64_t g_clip_rx_lines;

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
constexpr int kExecMaxEnv = 24;
constexpr int kRunAllPassLimit = 16;

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

void RemoveExecEnvByKey(char out[][128], const char** out_ptrs, int* io_count, const char* key) {
    if (out == nullptr || out_ptrs == nullptr || io_count == nullptr || key == nullptr) {
        return;
    }
    int count = *io_count;
    const int idx = FindExecEnvIndex(out_ptrs, count, key);
    if (idx < 0) {
        return;
    }
    for (int i = idx; i + 1 < count; ++i) {
        CopyString(out[i], out[i + 1], 128);
        out_ptrs[i] = out[i];
    }
    if (count > 0) {
        out[count - 1][0] = '\0';
        out_ptrs[count - 1] = nullptr;
        *io_count = count - 1;
    }
}

void TrimAsciiSpaces(char* s) {
    if (s == nullptr) {
        return;
    }
    int len = StrLength(s);
    int begin = 0;
    while (begin < len && (s[begin] == ' ' || s[begin] == '\t')) {
        ++begin;
    }
    int end = len;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    if (begin == 0 && end == len) {
        return;
    }
    int w = 0;
    for (int i = begin; i < end; ++i) {
        s[w++] = s[i];
    }
    s[w] = '\0';
}

bool ParseEnvFileBuffer(const uint8_t* data, uint64_t size,
                        char out_keys[][32], char out_values[][96],
                        int max_count, int* io_count) {
    if (data == nullptr || out_keys == nullptr || out_values == nullptr || io_count == nullptr) {
        return false;
    }
    char line[160];
    int line_len = 0;
    for (uint64_t i = 0; i <= size; ++i) {
        const char c = (i < size) ? static_cast<char>(data[i]) : '\n';
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (line_len + 1 < static_cast<int>(sizeof(line))) {
                line[line_len++] = c;
            }
            continue;
        }
        line[line_len] = '\0';
        line_len = 0;
        TrimAsciiSpaces(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        int eq = -1;
        for (int k = 0; line[k] != '\0'; ++k) {
            if (line[k] == '=') {
                eq = k;
                break;
            }
        }
        if (eq <= 0) {
            continue;
        }
        if (*io_count >= max_count) {
            return false;
        }
        char key[32];
        char value[96];
        int kw = 0;
        for (int k = 0; k < eq && kw + 1 < static_cast<int>(sizeof(key)); ++k) {
            key[kw++] = line[k];
        }
        key[kw] = '\0';
        int vw = 0;
        for (int k = eq + 1; line[k] != '\0' && vw + 1 < static_cast<int>(sizeof(value)); ++k) {
            value[vw++] = line[k];
        }
        value[vw] = '\0';
        TrimAsciiSpaces(key);
        TrimAsciiSpaces(value);
        if (key[0] == '\0') {
            continue;
        }
        CopyString(out_keys[*io_count], key, 32);
        CopyString(out_values[*io_count], value, 96);
        ++(*io_count);
    }
    return true;
}
}  // namespace

void PrintRunResultStart(const char* prefix, const scheduler::RunResult& result) {
    console->Print(prefix);
    console->Print("pid=");
    console->PrintDec(static_cast<int64_t>(result.queued_info.pid));
    console->Print(" path=");
    console->PrintLine(result.queued_info.path);
}

void PrintRunResultLine(const char* prefix, const scheduler::RunResult& result) {
    if (!result.ok) {
        console->Print(prefix);
        console->Print("failed: ");
        console->Print(result.queued_info.path);
        console->Print(" (");
        console->Print(usermode::GetLastRing3Error());
        console->Print(")\n");
        return;
    }
    if (result.final_info.state == proc::State::kYielded) {
        console->Print(prefix);
        console->Print("yielded -> ");
        console->PrintDec(static_cast<int64_t>(result.final_info.pid));
        console->Print("\n");
        return;
    }
    console->Print(prefix);
    console->Print("waitpid -> ");
    console->PrintDec(static_cast<int64_t>(result.final_info.pid));
    console->Print(" status=");
    console->PrintDec(result.wait_status);
    console->Print("\n");
}

bool ExecuteHelpCommand() {
    console->PrintLine("help: core  help clear tick time mem uptime echo reboot exec autosched runpid runnext runpass runall resumeall procs procq");
    console->PrintLine("help: proc  runnext=1 runnable, runpass/runall=ready pass, resumeall=yielded pass, procs=state, procq=queues");
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
    console->Print(" clip.rx.bytes=");
    console->PrintDec(static_cast<int64_t>(g_clip_rx_bytes));
    console->Print(" clip.rx.lines=");
    console->PrintDec(static_cast<int64_t>(g_clip_rx_lines));
    console->Print("\n");
    return true;
}

bool ExecuteSyscallCommand(const char* rest) {
    if (rest[0] == '\0' || StrEqual(rest, "stat")) {
        console->PrintLine("syscall abi=1 methods=write,tick,version,getenv,setenv,unsetenv,waitpid,yield trap=int80");
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

    if (StrStartsWith(rest, "setenv ")) {
        int pos = 0;
        char sub[16];
        char key[64];
        char value[96];
        NextToken(rest, &pos, sub, sizeof(sub)); // setenv
        if (!NextToken(rest, &pos, key, sizeof(key)) || !NextToken(rest, &pos, value, sizeof(value))) {
            console->PrintLine("usage: syscall setenv <key> <value>");
            return true;
        }
        char extra[8];
        if (NextToken(rest, &pos, extra, sizeof(extra))) {
            console->PrintLine("usage: syscall setenv <key> <value>");
            return true;
        }
        const int64_t ret = syscall::Dispatch(
            static_cast<uint64_t>(syscall::Number::kSetEnv),
            reinterpret_cast<uint64_t>(key),
            static_cast<uint64_t>(StrLength(key)),
            reinterpret_cast<uint64_t>(value),
            static_cast<uint64_t>(StrLength(value)));
        console->Print("syscall setenv -> ");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        return true;
    }

    if (StrStartsWith(rest, "unsetenv ")) {
        int pos = 0;
        char sub[16];
        char key[64];
        NextToken(rest, &pos, sub, sizeof(sub)); // unsetenv
        if (!NextToken(rest, &pos, key, sizeof(key))) {
            console->PrintLine("usage: syscall unsetenv <key>");
            return true;
        }
        char extra[8];
        if (NextToken(rest, &pos, extra, sizeof(extra))) {
            console->PrintLine("usage: syscall unsetenv <key>");
            return true;
        }
        const int64_t ret = syscall::Dispatch(
            static_cast<uint64_t>(syscall::Number::kUnsetEnv),
            reinterpret_cast<uint64_t>(key),
            static_cast<uint64_t>(StrLength(key)),
            0,
            0);
        console->Print("syscall unsetenv -> ");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        return true;
    }

    if (StrStartsWith(rest, "waitpid ")) {
        int pos = 0;
        char sub[16];
        char pid_text[16];
        char option[16];
        NextToken(rest, &pos, sub, sizeof(sub)); // waitpid
        if (!NextToken(rest, &pos, pid_text, sizeof(pid_text))) {
            console->PrintLine("usage: syscall waitpid <pid> [nohang]");
            return true;
        }
        bool nohang = false;
        if (NextToken(rest, &pos, option, sizeof(option))) {
            if (!StrEqual(option, "nohang")) {
                console->PrintLine("usage: syscall waitpid <pid> [nohang]");
                return true;
            }
            nohang = true;
            char extra[8];
            if (NextToken(rest, &pos, extra, sizeof(extra))) {
                console->PrintLine("usage: syscall waitpid <pid> [nohang]");
                return true;
            }
        }
        uint32_t pid = 0;
        for (int i = 0; pid_text[i] != '\0'; ++i) {
            if (pid_text[i] < '0' || pid_text[i] > '9') {
                console->PrintLine("syscall waitpid: pid must be decimal");
                return true;
            }
            pid = pid * 10u + static_cast<uint32_t>(pid_text[i] - '0');
        }
        int64_t status = 0;
        const int64_t ret = syscall::Dispatch(
            static_cast<uint64_t>(syscall::Number::kWaitPid),
            pid,
            reinterpret_cast<uint64_t>(&status),
            nohang ? 1 : 0,
            0);
        console->Print("syscall waitpid -> ");
        console->PrintDec(ret);
        if (ret > 0) {
            console->Print(" status=");
            console->PrintDec(status);
        } else if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        return true;
    }

    if (StrEqual(rest, "yield")) {
        const int64_t ret = syscall::Dispatch(static_cast<uint64_t>(syscall::Number::kYield), 0, 0, 0, 0);
        console->Print("syscall yield -> ");
        console->PrintDec(ret);
        console->Print("\n");
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

    if (StrEqual(rest, "trap yield")) {
        const int64_t ret = InvokeSyscallInt80(static_cast<uint64_t>(syscall::Number::kYield), 0, 0, 0, 0);
        console->Print("syscall trap yield -> ");
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

    if (StrStartsWith(rest, "trap setenv ")) {
        int pos = 0;
        char sub1[16];
        char sub2[16];
        char key[64];
        char value[96];
        NextToken(rest, &pos, sub1, sizeof(sub1)); // trap
        NextToken(rest, &pos, sub2, sizeof(sub2)); // setenv
        if (!NextToken(rest, &pos, key, sizeof(key)) || !NextToken(rest, &pos, value, sizeof(value))) {
            console->PrintLine("usage: syscall trap setenv <key> <value>");
            return true;
        }
        char extra[8];
        if (NextToken(rest, &pos, extra, sizeof(extra))) {
            console->PrintLine("usage: syscall trap setenv <key> <value>");
            return true;
        }
        const int64_t ret = InvokeSyscallInt80(
            static_cast<uint64_t>(syscall::Number::kSetEnv),
            reinterpret_cast<uint64_t>(key),
            static_cast<uint64_t>(StrLength(key)),
            reinterpret_cast<uint64_t>(value),
            static_cast<uint64_t>(StrLength(value)));
        console->Print("syscall trap setenv -> ");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        return true;
    }

    if (StrStartsWith(rest, "trap unsetenv ")) {
        int pos = 0;
        char sub1[16];
        char sub2[16];
        char key[64];
        NextToken(rest, &pos, sub1, sizeof(sub1)); // trap
        NextToken(rest, &pos, sub2, sizeof(sub2)); // unsetenv
        if (!NextToken(rest, &pos, key, sizeof(key))) {
            console->PrintLine("usage: syscall trap unsetenv <key>");
            return true;
        }
        char extra[8];
        if (NextToken(rest, &pos, extra, sizeof(extra))) {
            console->PrintLine("usage: syscall trap unsetenv <key>");
            return true;
        }
        const int64_t ret = InvokeSyscallInt80(
            static_cast<uint64_t>(syscall::Number::kUnsetEnv),
            reinterpret_cast<uint64_t>(key),
            static_cast<uint64_t>(StrLength(key)),
            0,
            0);
        console->Print("syscall trap unsetenv -> ");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
        return true;
    }

    console->PrintLine("usage: syscall [stat|tick|version|write <text>|getenv <key>|setenv <k> <v>|unsetenv <k>|waitpid <pid> [nohang]|yield|invalid|trap tick|trap version|trap write <text>|trap getenv <k>|trap setenv <k> <v>|trap unsetenv <k>|trap yield|trap invalid|trap badptr]");
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
    bool nowait = false;
    char env_opt_keys[kExecMaxEnv][32];
    char env_opt_values[kExecMaxEnv][96];
    int env_opt_count = 0;
    char env_file_keys[kExecMaxEnv][32];
    char env_file_values[kExecMaxEnv][96];
    int env_file_count = 0;
    char unset_env_keys[kExecMaxEnv][32];
    int unset_env_count = 0;

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
        if (StrEqual(tok, "--nowait")) {
            nowait = true;
            continue;
        }
        if (StrEqual(tok, "--unsetenv")) {
            char key[32];
            if (!NextToken(command, &pos, key, sizeof(key))) {
                console->PrintLine("exec: --unsetenv requires KEY");
                return true;
            }
            if (unset_env_count >= kExecMaxEnv) {
                console->PrintLine("exec: too many --unsetenv options");
                return true;
            }
            CopyString(unset_env_keys[unset_env_count], key, sizeof(unset_env_keys[unset_env_count]));
            ++unset_env_count;
            continue;
        }
        if (StrEqual(tok, "--env-file")) {
            char env_file_path[96];
            if (!NextToken(command, &pos, env_file_path, sizeof(env_file_path))) {
                console->PrintLine("exec: --env-file requires path");
                return true;
            }
            const ShellFile* user_file = FindShellFileByPath(g_cwd, env_file_path);
            const BootFileEntry* boot_file = nullptr;
            if (user_file == nullptr) {
                boot_file = FindBootFileByPath(g_cwd, env_file_path);
            }
            if (user_file == nullptr && boot_file == nullptr) {
                console->Print("exec: --env-file not found: ");
                console->PrintLine(env_file_path);
                return true;
            }
            const uint8_t* src_data = nullptr;
            uint64_t src_size = 0;
            if (user_file != nullptr) {
                src_data = user_file->data;
                src_size = user_file->size;
            } else {
                src_data = boot_file->data;
                src_size = boot_file->size;
            }
            if (!ParseEnvFileBuffer(src_data, src_size,
                                    env_file_keys, env_file_values,
                                    kExecMaxEnv, &env_file_count)) {
                console->PrintLine("exec: --env-file too many entries");
                return true;
            }
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
        console->PrintLine("usage: exec [--nowait] [--unsetenv KEY ...] [--env-file PATH ...] [--env KEY=VALUE ...] <bootfs-path> [args...]");
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
    for (int i = 0; i < unset_env_count; ++i) {
        RemoveExecEnvByKey(envs, env_ptrs, &envc, unset_env_keys[i]);
    }
    for (int i = 0; i < env_file_count; ++i) {
        if (!UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, env_file_keys[i], env_file_values[i])) {
            console->PrintLine("exec: env full (some --env-file entries dropped)");
            break;
        }
    }
    for (int i = 0; i < env_opt_count; ++i) {
        if (!UpsertExecEnv(envs, env_ptrs, kExecMaxEnv, &envc, env_opt_keys[i], env_opt_values[i])) {
            console->PrintLine("exec: env full (some --env dropped)");
            break;
        }
    }

    char resolved_path[96];
    if (!ResolveFilePath(g_cwd, path, resolved_path, sizeof(resolved_path))) {
        console->PrintLine("exec: invalid path");
        return true;
    }
    const BootFileEntry* file = FindBootFileByPath("/", resolved_path);
    uint32_t pid = 0;
    proc::CreateProcess(resolved_path, arg_ptrs, argc, env_ptrs, envc, &pid);
    if (file == nullptr) {
        console->Print("exec: not found: ");
        console->PrintLine(resolved_path);
        proc::MarkProcessFailed(pid, -1);
        return true;
    }
    console->Print("exec: pid=");
    console->PrintDec(static_cast<int64_t>(pid));
    console->Print(" path=");
    console->PrintLine(resolved_path);
    if (nowait) {
        console->PrintLine("exec: queued");
        return true;
    }
    scheduler::RunResult result{};
    if (scheduler::RunProcessWithResult(pid, FindBootFileByPath, &result)) {
        PrintRunResultLine("exec: ", result);
        if (result.final_info.state == proc::State::kYielded) {
            return true;
        }
        console->Print("exec: ok: ");
        console->PrintLine(resolved_path);
        const int64_t ret = result.wait_status;
        console->Print("exec.ret=");
        console->PrintDec(ret);
        if (ret < 0) {
            console->Print(" (");
            console->Print(syscall::ErrorName(ret));
            console->Print(")");
        }
        console->Print("\n");
    } else {
        PrintRunResultLine("exec: ", result);
        proc::MarkProcessFailed(pid, -1);
    }
    return true;
}

bool ExecuteRunNextCommand() {
    scheduler::RunResult result{};
    if (!scheduler::RunNextRunnableProcess(FindBootFileByPath, &result)) {
        console->PrintLine("runnext: no runnable process");
        return true;
    }
    PrintRunResultStart("runnext: ", result);
    PrintRunResultLine("runnext: ", result);
    return true;
}

bool ExecuteRunPidCommand(const char* rest) {
    if (rest == nullptr || rest[0] == '\0') {
        console->PrintLine("usage: runpid <pid>");
        return true;
    }
    uint32_t pid = 0;
    for (int i = 0; rest[i] != '\0'; ++i) {
        if (rest[i] < '0' || rest[i] > '9') {
            console->PrintLine("runpid: pid must be decimal");
            return true;
        }
        pid = pid * 10u + static_cast<uint32_t>(rest[i] - '0');
    }
    proc::Info info{};
    if (!proc::GetProcessInfo(pid, &info) || !info.used) {
        console->PrintLine("runpid: pid not found");
        return true;
    }
    if (info.state != proc::State::kReady && info.state != proc::State::kYielded) {
        console->Print("runpid: pid not runnable: ");
        console->PrintDec(static_cast<int64_t>(pid));
        console->Print("\n");
        return true;
    }
    scheduler::RunResult result{};
    if (!scheduler::RunPid(FindBootFileByPath, pid, &result)) {
        console->Print("runpid: pid=");
        console->PrintDec(static_cast<int64_t>(pid));
        console->Print("\n");
        PrintRunResultLine("runpid: ", result);
        return true;
    }
    PrintRunResultStart("runpid: ", result);
    PrintRunResultLine("runpid: ", result);
    return true;
}

bool ExecuteAutoSchedCommand(const char* rest) {
    if (rest == nullptr || rest[0] == '\0') {
        console->Print("autosched=");
        console->PrintLine(scheduler::IsAutoScheduleEnabled() ? "on" : "off");
        return true;
    }
    if (StrEqual(rest, "on")) {
        scheduler::SetAutoScheduleEnabled(true);
        console->PrintLine("autosched=on");
        return true;
    }
    if (StrEqual(rest, "off")) {
        scheduler::SetAutoScheduleEnabled(false);
        console->PrintLine("autosched=off");
        return true;
    }
    console->PrintLine("usage: autosched [on|off]");
    return true;
}

bool ExecuteRunAllCommand() {
    const proc::Summary summary = proc::GetProcessSummary();
    console->Print("runall: target.ready=");
    console->PrintDec(summary.ready);
    console->Print("\n");
    int ran = 0;
    while (ran < kRunAllPassLimit) {
        scheduler::RunResult result{};
        if (!scheduler::RunNextReadyProcess(FindBootFileByPath, &result)) {
            break;
        }
        PrintRunResultStart("runall: ", result);
        PrintRunResultLine("runall: ", result);
        ++ran;
        if (result.final_info.state == proc::State::kYielded) {
            break;
        }
    }
    console->Print("runall: ran=");
    console->PrintDec(ran);
    console->Print("\n");
    return true;
}

bool ExecuteResumeAllCommand() {
    const proc::Summary summary = proc::GetProcessSummary();
    console->Print("resumeall: target.yielded=");
    console->PrintDec(summary.yielded);
    console->Print("\n");
    int ran = 0;
    while (ran < kRunAllPassLimit) {
        scheduler::RunResult result{};
        if (!scheduler::RunAllYieldedProcesses(FindBootFileByPath, &result, 1)) {
            break;
        }
        PrintRunResultStart("resumeall: ", result);
        PrintRunResultLine("resumeall: ", result);
        ++ran;
        if (result.final_info.state == proc::State::kYielded) {
            break;
        }
    }
    console->Print("resumeall: ran=");
    console->PrintDec(ran);
    console->Print("\n");
    return true;
}

bool ExecuteProcsCommand() {
    console->Print("procs.autosched=");
    console->PrintLine(scheduler::IsAutoScheduleEnabled() ? "on" : "off");
    console->Print("procs.policy=");
    console->PrintLine(scheduler::PolicyName());
    console->PrintLine("pid state   argc yld rsp exit    start end   path");
    bool any = false;
    for (int n = 0; n < 16; ++n) {
        proc::Info info{};
        if (!proc::GetProcessInfoByRecentIndex(n, &info) || !info.used) {
            continue;
        }
        any = true;
        console->PrintDec(static_cast<int64_t>(info.pid));
        console->Print(" ");
        console->Print(proc::StateName(info.state));
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(info.argc));
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(info.yield_count));
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(info.resume_count));
        console->Print(" ");
        console->PrintDec(info.exit_code);
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(info.start_tick));
        console->Print(" ");
        console->PrintDec(static_cast<int64_t>(info.end_tick));
        console->Print(" ");
        console->PrintLine(info.path);
    }
    if (!any) {
        console->PrintLine("(no processes)");
    } else {
        const proc::Summary summary = proc::GetProcessSummary();
        console->Print("procs.total=");
        console->PrintDec(summary.total);
        console->Print(" runnable=");
        console->PrintDec(summary.runnable);
        console->Print(" ready=");
        console->PrintDec(summary.ready);
        console->Print(" yielded=");
        console->PrintDec(summary.yielded);
        console->Print("\n");
    }
    return true;
}

bool ExecuteProcQueueCommand() {
    proc::QueueSnapshot snapshot{};
    if (!proc::GetQueueSnapshot(&snapshot)) {
        console->PrintLine("procq: unavailable");
        return true;
    }
    const scheduler::Snapshot sched = scheduler::GetSnapshot();
    console->Print("procq.autosched=");
    console->PrintLine(sched.autosched_enabled ? "on" : "off");
    console->Print("procq.policy=");
    console->PrintLine(scheduler::PolicyName());
    console->Print("procq.tick.last=");
    console->PrintDec(static_cast<int64_t>(sched.last_autosched_tick));
    console->Print("\n");
    console->Print("procq.tick.count=");
    console->PrintDec(static_cast<int64_t>(sched.autosched_tick_count));
    console->Print("\n");
    console->Print("procq.run.count=");
    console->PrintDec(static_cast<int64_t>(sched.autosched_run_count));
    console->Print("\n");
    console->Print("procq.yield.count=");
    console->PrintDec(static_cast<int64_t>(sched.autosched_yield_count));
    console->Print("\n");
    console->Print("procq.tick.burst_remaining=");
    console->PrintDec(static_cast<int64_t>(sched.tick_burst_remaining));
    console->Print("\n");
    console->Print("procq.last_run=");
    console->PrintDec(static_cast<int64_t>(sched.last_run_pid));
    console->Print("\n");
    console->Print("procq.last_state=");
    console->PrintLine(proc::StateName(sched.last_run_state));
    console->Print("procq.last_status=");
    console->PrintDec(sched.last_wait_status);
    console->Print("\n");
    console->Print("procq.valid=");
    console->PrintLine(snapshot.valid ? "1" : "0");
    console->Print("procq.current=");
    console->PrintDec(static_cast<int64_t>(snapshot.current_pid));
    console->Print("\n");
    console->Print("procq.runnable.front=");
    console->PrintDec(static_cast<int64_t>(snapshot.runnable_front_pid));
    console->Print("\n");
    console->Print("procq.runnable.count=");
    console->PrintDec(snapshot.runnable_count);
    console->Print("\n");
    console->Print("procq.runnable=");
    for (int i = 0; i < snapshot.runnable_count && i < 16; ++i) {
        if (i > 0) {
            console->Print(" ");
        }
        console->PrintDec(static_cast<int64_t>(snapshot.runnable_pids[i]));
    }
    console->Print("\n");
    console->Print("procq.yielded.front=");
    console->PrintDec(static_cast<int64_t>(snapshot.yielded_front_pid));
    console->Print("\n");
    console->Print("procq.yielded.count=");
    console->PrintDec(snapshot.yielded_count);
    console->Print("\n");
    console->Print("procq.yielded=");
    for (int i = 0; i < snapshot.yielded_count && i < 16; ++i) {
        if (i > 0) {
            console->Print(" ");
        }
        console->PrintDec(static_cast<int64_t>(snapshot.yielded_pids[i]));
    }
    console->Print("\n");
    return true;
}

