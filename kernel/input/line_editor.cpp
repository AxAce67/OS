#include "input/line_editor.hpp"

#include "input/line_ops.hpp"
#include "input/selection.hpp"

namespace input {

bool DeleteSelection(char* buffer,
                     int buffer_capacity,
                     int* length,
                     int* cursor,
                     int* selection_anchor,
                     int* selection_end,
                     bool* selecting_with_mouse) {
    if (!HasSelection(*selection_anchor, *selection_end)) {
        return false;
    }
    int sel_start = SelectionStart(*selection_anchor, *selection_end);
    int sel_end = SelectionEnd(*selection_anchor, *selection_end);
    if (sel_start < 0) {
        sel_start = 0;
    }
    if (sel_end > *length) {
        sel_end = *length;
    }
    const int remove_len = sel_end - sel_start;
    if (remove_len <= 0) {
        ClearSelectionState(selection_anchor, selection_end, selecting_with_mouse);
        return false;
    }
    const bool changed = DeleteRange(buffer, buffer_capacity, length, cursor, sel_start, remove_len);
    *cursor = sel_start;
    ClearSelectionState(selection_anchor, selection_end, selecting_with_mouse);
    return changed;
}

bool BackspaceAtCursor(char* buffer,
                       int buffer_capacity,
                       int* length,
                       int* cursor) {
    if (buffer == nullptr || length == nullptr || cursor == nullptr) {
        return false;
    }
    if (*cursor <= 0 || *length <= 0) {
        return false;
    }
    return DeleteRange(buffer, buffer_capacity, length, cursor, *cursor - 1, 1);
}

bool DeleteAtCursor(char* buffer,
                    int buffer_capacity,
                    int* length,
                    int* cursor) {
    if (buffer == nullptr || length == nullptr || cursor == nullptr) {
        return false;
    }
    if (*length <= 0 || *cursor < 0 || *cursor >= *length) {
        return false;
    }
    return DeleteRange(buffer, buffer_capacity, length, cursor, *cursor, 1);
}

}  // namespace input

