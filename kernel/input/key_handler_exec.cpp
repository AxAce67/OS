#include "input/key_handler_exec.hpp"

namespace input {

void ClearRomajiInput(char* romaji_buffer, int romaji_capacity, int* romaji_len) {
    if (romaji_len != nullptr) {
        *romaji_len = 0;
    }
    if (romaji_buffer != nullptr && romaji_capacity > 0) {
        romaji_buffer[0] = '\0';
    }
}

void ResetLineForClear(char* command_buffer,
                       int command_capacity,
                       int* command_len,
                       int* cursor_pos,
                       int* rendered_len,
                       char* romaji_buffer,
                       int romaji_capacity,
                       int* romaji_len) {
    if (command_len != nullptr) {
        *command_len = 0;
    }
    if (cursor_pos != nullptr) {
        *cursor_pos = 0;
    }
    if (rendered_len != nullptr) {
        *rendered_len = 0;
    }
    if (command_buffer != nullptr && command_capacity > 0) {
        command_buffer[0] = '\0';
    }
    ClearRomajiInput(romaji_buffer, romaji_capacity, romaji_len);
}

int RestoreRomajiFromCandidate(const ImeCandidateEntry* entry,
                               char* romaji_buffer,
                               int romaji_capacity,
                               int (*str_length)(const char*)) {
    if (entry == nullptr || entry->key == nullptr ||
        romaji_buffer == nullptr || romaji_capacity <= 0 ||
        str_length == nullptr) {
        return 0;
    }
    const int src_len = str_length(entry->key);
    int len = 0;
    for (int i = 0; i < src_len && i + 1 < romaji_capacity; ++i) {
        romaji_buffer[i] = entry->key[i];
        len = i + 1;
    }
    romaji_buffer[len] = '\0';
    return len;
}

EscCancelState BuildEscCancelState(const ImeCandidateEntry* entry,
                                   int candidate_start,
                                   int candidate_len,
                                   char* romaji_buffer,
                                   int romaji_capacity,
                                   int (*str_length)(const char*)) {
    EscCancelState out{};
    out.delete_start = candidate_start;
    out.delete_len = candidate_len;
    out.cursor_after_delete = candidate_start;
    out.restored_romaji_len = RestoreRomajiFromCandidate(entry, romaji_buffer, romaji_capacity, str_length);
    out.valid = (entry != nullptr);
    return out;
}

void ResetForCtrlL(char* command_buffer,
                   int command_capacity,
                   int* command_len,
                   int* cursor_pos,
                   int* rendered_len,
                   char* romaji_buffer,
                   int romaji_capacity,
                   int* romaji_len) {
    ResetLineForClear(command_buffer,
                      command_capacity,
                      command_len,
                      cursor_pos,
                      rendered_len,
                      romaji_buffer,
                      romaji_capacity,
                      romaji_len);
}

void ApplyImeModeState(const ImeModeState& mode,
                       bool* ime_enabled,
                       bool* jp_layout) {
    if (ime_enabled != nullptr) {
        *ime_enabled = mode.ime_enabled;
    }
    if (jp_layout != nullptr) {
        *jp_layout = mode.jp_layout;
    }
}

void SetCursorValue(int* cursor_pos, int target) {
    if (cursor_pos != nullptr) {
        *cursor_pos = target;
    }
}

bool ShouldBrowseHistoryAfterCycle(bool cycle_succeeded) {
    return !cycle_succeeded;
}

bool ExecuteRegularNeutralAction(RegularExecKind kind,
                                 int command_len,
                                 const RegularNeutralCallbacks& callbacks,
                                 void* ctx) {
    switch (kind) {
    case RegularExecKind::kMoveCursorStart:
        if (callbacks.set_cursor_value != nullptr) {
            callbacks.set_cursor_value(ctx, 0);
            return true;
        }
        return false;
    case RegularExecKind::kMoveCursorEnd:
        if (callbacks.set_cursor_value != nullptr) {
            callbacks.set_cursor_value(ctx, command_len);
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool ExecuteRegularActionWithContext(RegularExecKind kind,
                                     const RegularActionContext& context) {
    switch (kind) {
    case RegularExecKind::kHistoryUpWithCandidate:
        if (context.cycle_candidate == nullptr ||
            context.browse_history_up == nullptr) {
            return false;
        }
        if (ShouldBrowseHistoryAfterCycle(context.cycle_candidate(context.owner, -1))) {
            context.browse_history_up(context.owner);
        }
        return true;
    case RegularExecKind::kHistoryDownWithCandidate:
        if (context.cycle_candidate == nullptr ||
            context.browse_history_down == nullptr) {
            return false;
        }
        if (ShouldBrowseHistoryAfterCycle(context.cycle_candidate(context.owner, 1))) {
            context.browse_history_down(context.owner);
        }
        return true;
    case RegularExecKind::kBackspace:
        if (context.backspace_at_cursor == nullptr) {
            return false;
        }
        context.backspace_at_cursor(context.owner);
        return true;
    case RegularExecKind::kDelete:
        if (context.delete_at_cursor == nullptr) {
            return false;
        }
        context.delete_at_cursor(context.owner);
        return true;
    case RegularExecKind::kTab:
        if (context.tab_complete == nullptr) {
            return false;
        }
        context.tab_complete(context.owner);
        return true;
    default:
        return false;
    }
}

bool ExecuteExtendedActionWithContext(ExtendedExecKind kind,
                                      const ExtendedActionContext& context) {
    switch (kind) {
    case ExtendedExecKind::kPageUp:
        if (context.scroll_up == nullptr) {
            return false;
        }
        context.scroll_up(context.owner, 3);
        return true;
    case ExtendedExecKind::kPageDown:
        if (context.scroll_down == nullptr) {
            return false;
        }
        context.scroll_down(context.owner, 3);
        return true;
    case ExtendedExecKind::kDelete:
        if (context.delete_at_cursor == nullptr) {
            return false;
        }
        context.delete_at_cursor(context.owner);
        return true;
    case ExtendedExecKind::kHistoryUp:
        if (context.browse_history_up == nullptr) {
            return false;
        }
        context.browse_history_up(context.owner);
        return true;
    case ExtendedExecKind::kHistoryDown:
        if (context.browse_history_down == nullptr) {
            return false;
        }
        context.browse_history_down(context.owner);
        return true;
    default:
        return false;
    }
}

ExtendedCursorMoveResult ExecuteExtendedCursorMoveAction(ExtendedExecKind kind,
                                                         int* cursor_pos,
                                                         int command_len) {
    ExtendedCursorMoveResult out{false, false};
    if (cursor_pos == nullptr) {
        return out;
    }
    switch (kind) {
    case ExtendedExecKind::kMoveCursorLeft:
        out.handled = true;
        if (*cursor_pos > 0) {
            --(*cursor_pos);
            out.should_render = true;
        }
        return out;
    case ExtendedExecKind::kMoveCursorRight:
        out.handled = true;
        if (*cursor_pos < command_len) {
            ++(*cursor_pos);
            out.should_render = true;
        }
        return out;
    case ExtendedExecKind::kMoveCursorStart:
        out.handled = true;
        *cursor_pos = 0;
        out.should_render = true;
        return out;
    case ExtendedExecKind::kMoveCursorEnd:
        out.handled = true;
        *cursor_pos = command_len;
        out.should_render = true;
        return out;
    default:
        return out;
    }
}

}  // namespace input
