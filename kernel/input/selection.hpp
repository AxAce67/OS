#pragma once

namespace input {

bool HasSelection(int anchor, int end);
int SelectionStart(int anchor, int end);
int SelectionEnd(int anchor, int end);
void ClearSelectionState(int* anchor, int* end, bool* selecting_with_mouse);

}  // namespace input

