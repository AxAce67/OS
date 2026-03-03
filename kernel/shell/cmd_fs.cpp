#include <stdint.h>
#include "console.hpp"
#include "boot_info.h"
#include "shell/context.hpp"
#include "shell/text.hpp"

extern Console* console;
extern const BootInfo* g_boot_info;
extern char g_cwd[96];
extern ShellDir g_dirs[32];
extern ShellFile g_files[64];

bool IsPrintableAscii(char c);
void InitializeDirectories();
bool ResolvePath(const char* cwd, const char* path, char* out, int out_len);
bool DirectoryExists(const char* path);
bool GetParentPath(const char* path, char* out, int out_len);
bool CreateDirectory(const char* path);
const ShellFile* FindShellFileByAbsPath(const char* abs_path);
ShellFile* FindShellFileByAbsPathMutable(const char* abs_path);
ShellFile* CreateShellFile(const char* abs_path);
bool ResolveFilePath(const char* cwd, const char* input, char* out, int out_len);
ShellDir* FindDirectoryMutable(const char* path);
bool IsPathSameOrChild(const char* path, const char* base);
bool IsDirectoryEmpty(const char* path);
bool BuildMovedPath(const char* current, const char* src_prefix, const char* dst_prefix, char* out, int out_len);
bool DirectoryExistsOutsideMove(const char* path, const char* src_prefix);
bool ShellFileExistsOutsideMove(const char* path, const char* src_prefix);
void GetBaseName(const char* path, char* out, int out_len);
void BuildBootFileAbsolutePath(const char* file_name, char* out, int out_len);
const ShellFile* FindShellFileByPath(const char* cwd, const char* input_path);
const BootFileEntry* FindBootFileByPath(const char* cwd, const char* input_path);
void PrintBootFile(const BootFileEntry* file);
void PrintBootFilePaged(const BootFileEntry* file, bool numbered);
void PrintBootFileNumbered(const BootFileEntry* file);
void PrintBootFileStat(const BootFileEntry* file);
void PrintShellFileStat(const ShellFile* file);

bool ExecuteFindCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char args[4][64];
    int argc = 0;
    while (true) {
        char tok[64];
        if (!NextToken(command, &pos, tok, sizeof(tok))) {
            break;
        }
        if (argc >= static_cast<int>(sizeof(args) / sizeof(args[0]))) {
            console->PrintLine("find: too many arguments");
            return true;
        }
        CopyString(args[argc], tok, sizeof(args[argc]));
        ++argc;
    }

    int type_filter = 0;  // 0=all, 1=file, 2=dir
    int idx = 0;
    while (idx < argc && StrEqual(args[idx], "-type")) {
        if (idx + 1 >= argc) {
            console->PrintLine("find: -type requires f or d");
            return true;
        }
        if (StrEqual(args[idx + 1], "f")) {
            type_filter = 1;
        } else if (StrEqual(args[idx + 1], "d")) {
            type_filter = 2;
        } else {
            console->PrintLine("find: -type must be f or d");
            return true;
        }
        idx += 2;
    }

    const int remain = argc - idx;
    char base_path[96];
    char pattern[64];
    pattern[0] = '\0';
    if (remain <= 0) {
        CopyString(base_path, g_cwd, sizeof(base_path));
    } else if (remain == 1) {
        char resolved1[96];
        bool arg1_is_dir = ResolvePath(g_cwd, args[idx], resolved1, sizeof(resolved1)) && DirectoryExists(resolved1);
        if (arg1_is_dir) {
            CopyString(base_path, resolved1, sizeof(base_path));
        } else {
            CopyString(base_path, g_cwd, sizeof(base_path));
            CopyString(pattern, args[idx], sizeof(pattern));
        }
    } else if (remain == 2) {
        char resolved1[96];
        if (!(ResolvePath(g_cwd, args[idx], resolved1, sizeof(resolved1)) && DirectoryExists(resolved1))) {
            console->Print("find: no such directory: ");
            console->PrintLine(args[idx]);
            return true;
        }
        CopyString(base_path, resolved1, sizeof(base_path));
        CopyString(pattern, args[idx + 1], sizeof(pattern));
    } else {
        console->PrintLine("find: too many arguments");
        return true;
    }

    int match_count = 0;
    auto PathMatches = [&](const char* p) {
        return pattern[0] == '\0' || StrContains(p, pattern);
    };

    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used) {
            continue;
        }
        if (!IsPathSameOrChild(g_dirs[i].path, base_path)) {
            continue;
        }
        if (type_filter == 1 || !PathMatches(g_dirs[i].path)) {
            continue;
        }
        console->Print(g_dirs[i].path);
        console->PrintLine("/");
        ++match_count;
    }

    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            continue;
        }
        if (!IsPathSameOrChild(g_files[i].path, base_path)) {
            continue;
        }
        if (type_filter == 2 || !PathMatches(g_files[i].path)) {
            continue;
        }
        console->PrintLine(g_files[i].path);
        ++match_count;
    }

    if (g_boot_info != nullptr && g_boot_info->boot_fs != nullptr) {
        const BootFileSystem* fs = g_boot_info->boot_fs;
        for (uint32_t i = 0; i < fs->file_count; ++i) {
            char abs_file_path[96];
            BuildBootFileAbsolutePath(fs->files[i].name, abs_file_path, sizeof(abs_file_path));
            if (FindShellFileByAbsPath(abs_file_path) != nullptr) {
                continue;
            }
            if (!IsPathSameOrChild(abs_file_path, base_path)) {
                continue;
            }
            if (type_filter == 2 || !PathMatches(abs_file_path)) {
                continue;
            }
            console->PrintLine(abs_file_path);
            ++match_count;
        }
    }

    if (match_count == 0) {
        console->PrintLine("find: no match");
    }
    return true;
}

bool ExecuteGrepCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    bool show_line_number = false;
    char files[8][64];
    int file_count = 0;
    char tok[64];
    if (!NextToken(command, &pos, tok, sizeof(tok))) {
        console->PrintLine("grep: pattern and file required");
        return true;
    }
    while (tok[0] == '-') {
        if (StrEqual(tok, "-n")) {
            show_line_number = true;
        } else {
            console->Print("grep: unknown option: ");
            console->PrintLine(tok);
            return true;
        }
        if (!NextToken(command, &pos, tok, sizeof(tok))) {
            console->PrintLine("grep: pattern and file required");
            return true;
        }
    }
    char pattern[64];
    CopyString(pattern, tok, sizeof(pattern));
    while (NextToken(command, &pos, tok, sizeof(tok))) {
        if (file_count >= static_cast<int>(sizeof(files) / sizeof(files[0]))) {
            console->PrintLine("grep: too many files");
            return true;
        }
        CopyString(files[file_count], tok, sizeof(files[file_count]));
        ++file_count;
    }
    if (file_count == 0) {
        console->PrintLine("grep: file required");
        return true;
    }

    const int pat_len = StrLength(pattern);
    bool matched = false;
    const bool multi_file = file_count > 1;
    auto SearchOne = [&](const char* display_name, const uint8_t* data, uint64_t size) {
        auto LineContains = [&](uint64_t begin, uint64_t end) {
            if (pat_len == 0) {
                return true;
            }
            if (end < begin) {
                return false;
            }
            uint64_t len = end - begin;
            if (len < static_cast<uint64_t>(pat_len)) {
                return false;
            }
            for (uint64_t i = begin; i + static_cast<uint64_t>(pat_len) <= end; ++i) {
                bool ok = true;
                for (int j = 0; j < pat_len; ++j) {
                    if (static_cast<char>(data[i + static_cast<uint64_t>(j)]) != pattern[j]) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    return true;
                }
            }
            return false;
        };

        uint64_t line_begin = 0;
        int line_no = 1;
        for (uint64_t i = 0; i <= size; ++i) {
            if (i < size && data[i] != '\n') {
                continue;
            }
            uint64_t line_end = i;
            if (LineContains(line_begin, line_end)) {
                matched = true;
                if (multi_file) {
                    console->Print(display_name);
                    console->Print(":");
                }
                if (show_line_number) {
                    console->PrintDec(line_no);
                    console->Print(": ");
                }
                for (uint64_t k = line_begin; k < line_end; ++k) {
                    char c = static_cast<char>(data[k]);
                    if (c == '\r') {
                        continue;
                    }
                    if (c == '\t' || IsPrintableAscii(c)) {
                        char s[2] = {c, '\0'};
                        console->Print(s);
                    } else {
                        console->Print(".");
                    }
                }
                console->Print("\n");
            }
            line_begin = i + 1;
            ++line_no;
        }
    };

    for (int fi = 0; fi < file_count; ++fi) {
        const uint8_t* data = nullptr;
        uint64_t size = 0;
        const ShellFile* user_file = FindShellFileByPath(g_cwd, files[fi]);
        if (user_file != nullptr) {
            data = user_file->data;
            size = user_file->size;
        } else {
            const BootFileEntry* boot_file = FindBootFileByPath(g_cwd, files[fi]);
            if (boot_file == nullptr) {
                console->Print("grep: not found: ");
                console->PrintLine(files[fi]);
                continue;
            }
            data = boot_file->data;
            size = boot_file->size;
        }
        SearchOne(files[fi], data, size);
    }
    if (!matched) {
        console->PrintLine("grep: no match");
    }
    return true;
}

bool ExecuteLsCommand(const char* rest) {
    struct LsEntry {
        char name[64];
        bool is_dir;
        uint64_t size;
        int kind; // 0=dir, 1=user file, 2=boot file
    };
    LsEntry entries[128];
    int entry_count = 0;
    auto CopyLsEntry = [&](LsEntry* dst, const LsEntry* src) {
        CopyString(dst->name, src->name, sizeof(dst->name));
        dst->is_dir = src->is_dir;
        dst->size = src->size;
        dst->kind = src->kind;
    };
    auto AddLsEntry = [&](const char* name, bool is_dir, uint64_t size, int kind) {
        if (entry_count >= static_cast<int>(sizeof(entries) / sizeof(entries[0]))) {
            return;
        }
        CopyString(entries[entry_count].name, name, sizeof(entries[entry_count].name));
        entries[entry_count].is_dir = is_dir;
        entries[entry_count].size = size;
        entries[entry_count].kind = kind;
        ++entry_count;
    };

    bool long_format = false;
    char target_arg[64];
    target_arg[0] = '\0';
    int arg_pos = 0;
    char arg[64];
    while (NextToken(rest, &arg_pos, arg, sizeof(arg))) {
        if (arg[0] == '-') {
            if (StrEqual(arg, "-l")) {
                long_format = true;
            } else {
                console->Print("ls: unknown option: ");
                console->PrintLine(arg);
                return true;
            }
            continue;
        }
        if (target_arg[0] != '\0') {
            console->PrintLine("ls: too many paths");
            return true;
        }
        CopyString(target_arg, arg, sizeof(target_arg));
    }

    char target_dir[96];
    if (target_arg[0] == '\0') {
        CopyString(target_dir, g_cwd, sizeof(target_dir));
    } else if (!ResolvePath(g_cwd, target_arg, target_dir, sizeof(target_dir))) {
        console->PrintLine("ls: invalid path");
        return true;
    }
    if (!DirectoryExists(target_dir)) {
        console->Print("ls: no such directory: ");
        console->PrintLine(target_arg[0] == '\0' ? target_dir : target_arg);
        return true;
    }

    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used || StrEqual(g_dirs[i].path, "/")) {
            continue;
        }
        char parent[96];
        if (!GetParentPath(g_dirs[i].path, parent, sizeof(parent))) {
            continue;
        }
        if (!StrEqual(parent, target_dir)) {
            continue;
        }
        char base[64];
        GetBaseName(g_dirs[i].path, base, sizeof(base));
        AddLsEntry(base, true, 0, 0);
    }

    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            continue;
        }
        char parent[96];
        if (!GetParentPath(g_files[i].path, parent, sizeof(parent))) {
            continue;
        }
        if (!StrEqual(parent, target_dir)) {
            continue;
        }
        char base[64];
        GetBaseName(g_files[i].path, base, sizeof(base));
        AddLsEntry(base, false, g_files[i].size, 1);
    }

    if (g_boot_info != nullptr && g_boot_info->boot_fs != nullptr) {
        const BootFileSystem* fs = g_boot_info->boot_fs;
        for (uint32_t i = 0; i < fs->file_count; ++i) {
            char abs_file_path[96];
            BuildBootFileAbsolutePath(fs->files[i].name, abs_file_path, sizeof(abs_file_path));
            if (FindShellFileByAbsPath(abs_file_path) != nullptr) {
                continue;
            }

            char parent[96];
            if (!GetParentPath(abs_file_path, parent, sizeof(parent))) {
                continue;
            }
            if (!StrEqual(parent, target_dir)) {
                continue;
            }

            char base[64];
            GetBaseName(abs_file_path, base, sizeof(base));
            AddLsEntry(base, false, fs->files[i].size, 2);
        }
    }

    for (int i = 1; i < entry_count; ++i) {
        LsEntry key;
        CopyLsEntry(&key, &entries[i]);
        int j = i - 1;
        while (j >= 0) {
            int cmp = StrCompare(entries[j].name, key.name);
            if (cmp < 0) {
                break;
            }
            if (cmp == 0) {
                if (entries[j].is_dir && !key.is_dir) {
                    break;
                }
                if (entries[j].is_dir == key.is_dir && entries[j].kind <= key.kind) {
                    break;
                }
            }
            CopyLsEntry(&entries[j + 1], &entries[j]);
            --j;
        }
        CopyLsEntry(&entries[j + 1], &key);
    }

    for (int i = 0; i < entry_count; ++i) {
        if (long_format) {
            if (entries[i].is_dir) {
                console->Print("drwxr-xr-x ");
                console->PrintDec(0);
                console->Print(" B ");
                console->Print(entries[i].name);
                console->PrintLine("/");
            } else if (entries[i].kind == 1) {
                console->Print("-rw-rw-r-- ");
                console->PrintDec(static_cast<int64_t>(entries[i].size));
                console->Print(" B ");
                console->PrintLine(entries[i].name);
            } else {
                console->Print("-rw-r--r-- ");
                console->PrintDec(static_cast<int64_t>(entries[i].size));
                console->Print(" B ");
                console->PrintLine(entries[i].name);
            }
        } else {
            console->Print(entries[i].name);
            if (entries[i].is_dir) {
                console->Print("/");
            }
            console->Print("\n");
        }
    }
    return true;
}

bool ExecuteStatCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char name[64];
    if (!NextToken(command, &pos, name, sizeof(name))) {
        console->PrintLine("stat: filename required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("stat: too many arguments");
        return true;
    }
    const ShellFile* user_file = FindShellFileByPath(g_cwd, name);
    if (user_file != nullptr) {
        PrintShellFileStat(user_file);
        return true;
    }
    const BootFileEntry* boot_file = FindBootFileByPath(g_cwd, name);
    if (boot_file == nullptr) {
        console->Print("stat: not found: ");
        console->PrintLine(name);
        return true;
    }
    PrintBootFileStat(boot_file);
    return true;
}

bool ExecuteCatCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    bool show_line_number = false;
    bool paged = false;
    char name[64];
    if (!NextToken(command, &pos, name, sizeof(name))) {
        console->PrintLine("cat: filename required");
        return true;
    }
    while (name[0] == '-') {
        if (StrEqual(name, "-n")) {
            show_line_number = true;
        } else if (StrEqual(name, "-p")) {
            paged = true;
        } else {
            console->Print("cat: unknown option: ");
            console->PrintLine(name);
            return true;
        }
        if (!NextToken(command, &pos, name, sizeof(name))) {
            console->PrintLine("cat: filename required");
            return true;
        }
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("cat: too many arguments");
        return true;
    }
    const ShellFile* user_file = FindShellFileByPath(g_cwd, name);
    if (user_file != nullptr) {
        BootFileEntry temp;
        temp.size = user_file->size;
        temp.data = const_cast<uint8_t*>(user_file->data);
        if (paged) {
            PrintBootFilePaged(&temp, show_line_number);
        } else if (show_line_number) {
            PrintBootFileNumbered(&temp);
        } else {
            PrintBootFile(&temp);
        }
        return true;
    }
    const BootFileEntry* boot_file = FindBootFileByPath(g_cwd, name);
    if (boot_file == nullptr) {
        console->Print("cat: not found: ");
        console->PrintLine(name);
        return true;
    }
    if (paged) {
        PrintBootFilePaged(boot_file, show_line_number);
    } else if (show_line_number) {
        PrintBootFileNumbered(boot_file);
    } else {
        PrintBootFile(boot_file);
    }
    return true;
}

bool ExecuteCdCommand(const char* rest) {
    InitializeDirectories();
    if (rest[0] == '\0') {
        CopyString(g_cwd, "/", sizeof(g_cwd));
        return true;
    }
    char resolved[96];
    if (!ResolvePath(g_cwd, rest, resolved, sizeof(resolved))) {
        console->PrintLine("cd: invalid path");
        return true;
    }
    if (!DirectoryExists(resolved)) {
        console->Print("cd: no such directory: ");
        console->PrintLine(rest);
        return true;
    }
    CopyString(g_cwd, resolved, sizeof(g_cwd));
    return true;
}

bool ExecuteMkdirCommand(const char* command, int* pos_ptr) {
    InitializeDirectories();
    int& pos = *pos_ptr;
    char name[64];
    if (!NextToken(command, &pos, name, sizeof(name))) {
        console->PrintLine("mkdir: directory required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("mkdir: too many arguments");
        return true;
    }
    char resolved[96];
    if (!ResolvePath(g_cwd, name, resolved, sizeof(resolved))) {
        console->PrintLine("mkdir: invalid path");
        return true;
    }
    if (StrEqual(resolved, "/")) {
        console->PrintLine("mkdir: already exists: /");
        return true;
    }
    if (DirectoryExists(resolved)) {
        console->Print("mkdir: already exists: ");
        console->PrintLine(resolved);
        return true;
    }
    char parent[96];
    if (!GetParentPath(resolved, parent, sizeof(parent))) {
        console->PrintLine("mkdir: invalid parent");
        return true;
    }
    if (!DirectoryExists(parent)) {
        console->Print("mkdir: parent missing: ");
        console->PrintLine(parent);
        return true;
    }
    if (!CreateDirectory(resolved)) {
        console->PrintLine("mkdir: table full");
        return true;
    }
    return true;
}

bool ExecuteTouchCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char name[64];
    if (!NextToken(command, &pos, name, sizeof(name))) {
        console->PrintLine("touch: filename required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("touch: too many arguments");
        return true;
    }
    char resolved_path[96];
    if (!ResolveFilePath(g_cwd, name, resolved_path, sizeof(resolved_path))) {
        console->PrintLine("touch: invalid path");
        return true;
    }
    if (FindShellFileByAbsPath(resolved_path) != nullptr) {
        return true;
    }
    if (FindBootFileByPath(g_cwd, name) != nullptr) {
        console->PrintLine("touch: read-only boot file exists");
        return true;
    }
    if (CreateShellFile(resolved_path) == nullptr) {
        console->PrintLine("touch: file table full");
        return true;
    }
    return true;
}

bool ExecuteWriteAppendCommand(const char* cmd, const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char name[64];
    if (!NextToken(command, &pos, name, sizeof(name))) {
        console->Print(StrEqual(cmd, "write") ? "write: filename required\n" : "append: filename required\n");
        return true;
    }
    const char* text = RestOfLine(command, pos);
    char resolved_path[96];
    if (!ResolveFilePath(g_cwd, name, resolved_path, sizeof(resolved_path))) {
        console->Print(StrEqual(cmd, "write") ? "write: invalid path\n" : "append: invalid path\n");
        return true;
    }
    if (FindBootFileByPath(g_cwd, name) != nullptr) {
        console->Print(StrEqual(cmd, "write") ? "write: boot file is read-only\n" : "append: boot file is read-only\n");
        return true;
    }
    ShellFile* file = FindShellFileByAbsPathMutable(resolved_path);
    if (file == nullptr) {
        file = CreateShellFile(resolved_path);
        if (file == nullptr) {
            console->PrintLine("write: file table full");
            return true;
        }
    }

    const int text_len = StrLength(text);
    if (StrEqual(cmd, "write")) {
        const int max_write = static_cast<int>(sizeof(file->data));
        int copy_len = (text_len < max_write) ? text_len : max_write;
        for (int i = 0; i < copy_len; ++i) {
            file->data[i] = static_cast<uint8_t>(text[i]);
        }
        file->size = static_cast<uint64_t>(copy_len);
        if (copy_len < text_len) {
            console->PrintLine("write: truncated");
        }
        return true;
    }

    const int max_size = static_cast<int>(sizeof(file->data));
    int cur_size = static_cast<int>(file->size);
    if (cur_size >= max_size) {
        console->PrintLine("append: file full");
        return true;
    }
    int writable = max_size - cur_size;
    int copy_len = (text_len < writable) ? text_len : writable;
    for (int i = 0; i < copy_len; ++i) {
        file->data[cur_size + i] = static_cast<uint8_t>(text[i]);
    }
    file->size = static_cast<uint64_t>(cur_size + copy_len);
    if (copy_len < text_len) {
        console->PrintLine("append: truncated");
    }
    return true;
}

bool ExecuteCpCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char src_name[64];
    char dst_name[64];
    if (!NextToken(command, &pos, src_name, sizeof(src_name)) ||
        !NextToken(command, &pos, dst_name, sizeof(dst_name))) {
        console->PrintLine("cp: src and dst required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("cp: too many arguments");
        return true;
    }

    char dst_path[96];
    if (!ResolveFilePath(g_cwd, dst_name, dst_path, sizeof(dst_path))) {
        console->PrintLine("cp: invalid destination");
        return true;
    }
    if (DirectoryExists(dst_path)) {
        console->PrintLine("cp: destination is directory");
        return true;
    }
    if (FindBootFileByPath("/", dst_path) != nullptr && FindShellFileByAbsPath(dst_path) == nullptr) {
        console->PrintLine("cp: destination conflicts with boot file");
        return true;
    }

    const ShellFile* src_user_file = FindShellFileByPath(g_cwd, src_name);
    const BootFileEntry* src_boot_file = nullptr;
    if (src_user_file == nullptr) {
        src_boot_file = FindBootFileByPath(g_cwd, src_name);
    }
    if (src_user_file == nullptr && src_boot_file == nullptr) {
        console->Print("cp: not found: ");
        console->PrintLine(src_name);
        return true;
    }

    uint64_t src_size = 0;
    const uint8_t* src_data = nullptr;
    if (src_user_file != nullptr) {
        src_size = src_user_file->size;
        src_data = src_user_file->data;
    } else {
        src_size = src_boot_file->size;
        src_data = src_boot_file->data;
    }

    ShellFile* dst_file = FindShellFileByAbsPathMutable(dst_path);
    if (dst_file == nullptr) {
        dst_file = CreateShellFile(dst_path);
        if (dst_file == nullptr) {
            console->PrintLine("cp: file table full");
            return true;
        }
    }

    const uint64_t max_size = sizeof(dst_file->data);
    uint64_t copy_len = (src_size < max_size) ? src_size : max_size;
    for (uint64_t i = 0; i < copy_len; ++i) {
        dst_file->data[i] = src_data[i];
    }
    dst_file->size = copy_len;
    if (copy_len < src_size) {
        console->PrintLine("cp: truncated");
    }
    return true;
}

bool ExecuteRmCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char target[64];
    if (!NextToken(command, &pos, target, sizeof(target))) {
        console->PrintLine("rm: path required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("rm: too many arguments");
        return true;
    }
    char resolved_path[96];
    if (!ResolvePath(g_cwd, target, resolved_path, sizeof(resolved_path))) {
        console->PrintLine("rm: invalid path");
        return true;
    }
    if (StrEqual(resolved_path, "/")) {
        console->PrintLine("rm: cannot remove /");
        return true;
    }
    if (IsPathSameOrChild(g_cwd, resolved_path)) {
        console->PrintLine("rm: path is in use");
        return true;
    }

    ShellFile* file = FindShellFileByAbsPathMutable(resolved_path);
    if (file != nullptr) {
        file->used = false;
        file->path[0] = '\0';
        file->size = 0;
        return true;
    }

    if (FindBootFileByPath(g_cwd, target) != nullptr) {
        console->PrintLine("rm: boot file is read-only");
        return true;
    }

    ShellDir* dir = FindDirectoryMutable(resolved_path);
    if (dir != nullptr) {
        if (!IsDirectoryEmpty(resolved_path)) {
            console->PrintLine("rm: directory not empty");
            return true;
        }
        dir->used = false;
        dir->path[0] = '\0';
        return true;
    }

    console->Print("rm: not found: ");
    console->PrintLine(target);
    return true;
}

bool ExecuteRmdirCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char target[64];
    if (!NextToken(command, &pos, target, sizeof(target))) {
        console->PrintLine("rmdir: path required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("rmdir: too many arguments");
        return true;
    }
    char resolved_path[96];
    if (!ResolvePath(g_cwd, target, resolved_path, sizeof(resolved_path))) {
        console->PrintLine("rmdir: invalid path");
        return true;
    }
    if (StrEqual(resolved_path, "/")) {
        console->PrintLine("rmdir: cannot remove /");
        return true;
    }
    if (IsPathSameOrChild(g_cwd, resolved_path)) {
        console->PrintLine("rmdir: path is in use");
        return true;
    }
    ShellDir* dir = FindDirectoryMutable(resolved_path);
    if (dir == nullptr) {
        if (FindBootFileByPath(g_cwd, target) != nullptr || FindShellFileByPath(g_cwd, target) != nullptr) {
            console->PrintLine("rmdir: not a directory");
        } else {
            console->Print("rmdir: not found: ");
            console->PrintLine(target);
        }
        return true;
    }
    if (!IsDirectoryEmpty(resolved_path)) {
        console->PrintLine("rmdir: directory not empty");
        return true;
    }
    dir->used = false;
    dir->path[0] = '\0';
    return true;
}

bool ExecuteMvCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char src_name[64];
    char dst_name[64];
    if (!NextToken(command, &pos, src_name, sizeof(src_name)) ||
        !NextToken(command, &pos, dst_name, sizeof(dst_name))) {
        console->PrintLine("mv: src and dst required");
        return true;
    }
    char extra[8];
    if (NextToken(command, &pos, extra, sizeof(extra))) {
        console->PrintLine("mv: too many arguments");
        return true;
    }

    char src_path[96];
    char dst_path[96];
    if (!ResolvePath(g_cwd, src_name, src_path, sizeof(src_path)) ||
        !ResolvePath(g_cwd, dst_name, dst_path, sizeof(dst_path))) {
        console->PrintLine("mv: invalid path");
        return true;
    }
    if (StrEqual(src_path, "/")) {
        console->PrintLine("mv: cannot move /");
        return true;
    }

    char dst_parent[96];
    if (!GetParentPath(dst_path, dst_parent, sizeof(dst_parent)) || !DirectoryExists(dst_parent)) {
        console->PrintLine("mv: destination parent missing");
        return true;
    }
    if (DirectoryExists(dst_path) || FindShellFileByAbsPath(dst_path) != nullptr) {
        console->PrintLine("mv: destination exists");
        return true;
    }
    if (FindBootFileByPath("/", dst_path) != nullptr) {
        console->PrintLine("mv: destination conflicts with boot file");
        return true;
    }

    ShellFile* src_file = FindShellFileByAbsPathMutable(src_path);
    if (src_file != nullptr) {
        src_file->path[0] = '\0';
        CopyString(src_file->path, dst_path, sizeof(src_file->path));
        return true;
    }

    if (FindBootFileByPath("/", src_path) != nullptr) {
        console->PrintLine("mv: boot file is read-only");
        return true;
    }

    ShellDir* src_dir = FindDirectoryMutable(src_path);
    if (src_dir == nullptr) {
        console->Print("mv: not found: ");
        console->PrintLine(src_name);
        return true;
    }
    if (IsPathSameOrChild(dst_path, src_path)) {
        console->PrintLine("mv: invalid destination");
        return true;
    }

    char new_path[96];
    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used || !IsPathSameOrChild(g_dirs[i].path, src_path)) {
            continue;
        }
        if (!BuildMovedPath(g_dirs[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
            console->PrintLine("mv: path too long");
            return true;
        }
        if (DirectoryExistsOutsideMove(new_path, src_path) ||
            ShellFileExistsOutsideMove(new_path, src_path)) {
            console->PrintLine("mv: destination exists");
            return true;
        }
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used || !IsPathSameOrChild(g_files[i].path, src_path)) {
            continue;
        }
        if (!BuildMovedPath(g_files[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
            console->PrintLine("mv: path too long");
            return true;
        }
        if (DirectoryExistsOutsideMove(new_path, src_path) ||
            ShellFileExistsOutsideMove(new_path, src_path)) {
            console->PrintLine("mv: destination exists");
            return true;
        }
        if (FindBootFileByPath("/", new_path) != nullptr) {
            console->PrintLine("mv: destination conflicts with boot file");
            return true;
        }
    }

    for (int i = 0; i < static_cast<int>(sizeof(g_dirs) / sizeof(g_dirs[0])); ++i) {
        if (!g_dirs[i].used) {
            continue;
        }
        if (!IsPathSameOrChild(g_dirs[i].path, src_path)) {
            continue;
        }
        if (!BuildMovedPath(g_dirs[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
            console->PrintLine("mv: path too long");
            return true;
        }
        CopyString(g_dirs[i].path, new_path, sizeof(g_dirs[i].path));
    }
    for (int i = 0; i < static_cast<int>(sizeof(g_files) / sizeof(g_files[0])); ++i) {
        if (!g_files[i].used) {
            continue;
        }
        if (!IsPathSameOrChild(g_files[i].path, src_path)) {
            continue;
        }
        if (!BuildMovedPath(g_files[i].path, src_path, dst_path, new_path, sizeof(new_path))) {
            console->PrintLine("mv: path too long");
            return true;
        }
        CopyString(g_files[i].path, new_path, sizeof(g_files[i].path));
    }
    if (IsPathSameOrChild(g_cwd, src_path) &&
        BuildMovedPath(g_cwd, src_path, dst_path, new_path, sizeof(new_path))) {
        CopyString(g_cwd, new_path, sizeof(g_cwd));
    }
    return true;
}

