#include "shell/tab_completion.hpp"

namespace shell {

void ComputeTabCompletion(const TabCompletionContext& c, TabCompletionResult* out) {
    if (out == nullptr) {
        return;
    }
    out->action = TabCompletionAction::kNone;
    out->candidate_count = 0;
    out->replacement[0] = '\0';
    if (c.command_buffer == nullptr || c.cwd == nullptr) {
        return;
    }
    if (c.command_len == 0 || c.cursor_pos != c.command_len) {
        return;
    }

    int token_start = c.cursor_pos;
    while (token_start > 0 && c.command_buffer[token_start - 1] != ' ') {
        --token_start;
    }

    char token[64];
    int tw = 0;
    for (int i = token_start; i < c.cursor_pos && tw + 1 < static_cast<int>(sizeof(token)); ++i) {
        token[tw++] = c.command_buffer[i];
    }
    token[tw] = '\0';

    const bool first_token = (token_start == 0);

    auto AddCandidate = [&](const char* name) {
        for (int i = 0; i < out->candidate_count; ++i) {
            if (c.str_equal(out->candidates[i], name)) {
                return;
            }
        }
        if (out->candidate_count >= static_cast<int>(sizeof(out->candidates) / sizeof(out->candidates[0]))) {
            return;
        }
        c.copy_string(out->candidates[out->candidate_count], name,
                      static_cast<int>(sizeof(out->candidates[out->candidate_count])));
        ++out->candidate_count;
    };

    if (first_token) {
        for (int i = 0; i < c.builtin_command_count; ++i) {
            if (c.str_starts_with(c.builtin_commands[i], token)) {
                AddCandidate(c.builtin_commands[i]);
            }
        }
    } else {
        if (c.contains_char(token, '/')) {
            return;
        }
        for (int i = 0; i < c.dir_count; ++i) {
            if (!c.dirs[i].used || c.str_equal(c.dirs[i].path, "/")) {
                continue;
            }
            char parent[96];
            if (!c.get_parent_path(c.dirs[i].path, parent, static_cast<int>(sizeof(parent))) ||
                !c.str_equal(parent, c.cwd)) {
                continue;
            }
            char base[64];
            c.get_base_name(c.dirs[i].path, base, static_cast<int>(sizeof(base)));
            if (!c.str_starts_with(base, token)) {
                continue;
            }
            char dname[64];
            c.copy_string(dname, base, static_cast<int>(sizeof(dname)));
            int n = c.str_length(dname);
            if (n + 1 < static_cast<int>(sizeof(dname))) {
                dname[n] = '/';
                dname[n + 1] = '\0';
            }
            AddCandidate(dname);
        }
        for (int i = 0; i < c.file_count; ++i) {
            if (!c.files[i].used) {
                continue;
            }
            char parent[96];
            if (!c.get_parent_path(c.files[i].path, parent, static_cast<int>(sizeof(parent))) ||
                !c.str_equal(parent, c.cwd)) {
                continue;
            }
            char base[64];
            c.get_base_name(c.files[i].path, base, static_cast<int>(sizeof(base)));
            if (c.str_starts_with(base, token)) {
                AddCandidate(base);
            }
        }
        if (c.boot_info != nullptr && c.boot_info->boot_fs != nullptr) {
            const BootFileSystem* fs = c.boot_info->boot_fs;
            for (uint32_t i = 0; i < fs->file_count; ++i) {
                char abs_file_path[96];
                c.build_boot_file_absolute_path(fs->files[i].name, abs_file_path, static_cast<int>(sizeof(abs_file_path)));
                if (c.find_shell_file_by_abs_path(abs_file_path) != nullptr) {
                    continue;
                }
                char parent[96];
                if (!c.get_parent_path(abs_file_path, parent, static_cast<int>(sizeof(parent))) ||
                    !c.str_equal(parent, c.cwd)) {
                    continue;
                }
                char base[64];
                c.get_base_name(abs_file_path, base, static_cast<int>(sizeof(base)));
                if (c.str_starts_with(base, token)) {
                    AddCandidate(base);
                }
            }
        }
    }

    if (out->candidate_count == 0) {
        return;
    }
    if (out->candidate_count == 1) {
        const char* single_match = out->candidates[0];
        int w = 0;
        for (int i = 0; i < token_start && w + 1 < static_cast<int>(sizeof(out->replacement)); ++i) {
            out->replacement[w++] = c.command_buffer[i];
        }
        for (int i = 0; single_match[i] != '\0' && w + 1 < static_cast<int>(sizeof(out->replacement)); ++i) {
            out->replacement[w++] = single_match[i];
        }
        out->replacement[w] = '\0';
        out->action = TabCompletionAction::kReplaceLine;
        return;
    }
    out->action = TabCompletionAction::kShowCandidates;
}

}  // namespace shell

