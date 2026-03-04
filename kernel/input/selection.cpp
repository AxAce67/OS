#include "input/selection.hpp"

namespace input {

bool HasSelection(int anchor, int end) {
    return anchor >= 0 && end >= 0 && anchor != end;
}

int SelectionStart(int anchor, int end) {
    return (anchor < end) ? anchor : end;
}

int SelectionEnd(int anchor, int end) {
    return (anchor > end) ? anchor : end;
}

void ClearSelectionState(int* anchor, int* end, bool* selecting_with_mouse) {
    if (anchor != nullptr) {
        *anchor = -1;
    }
    if (end != nullptr) {
        *end = -1;
    }
    if (selecting_with_mouse != nullptr) {
        *selecting_with_mouse = false;
    }
}

}  // namespace input

