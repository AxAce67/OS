#pragma once

namespace input {

bool DeleteSelection(char* buffer,
                     int buffer_capacity,
                     int* length,
                     int* cursor,
                     int* selection_anchor,
                     int* selection_end,
                     bool* selecting_with_mouse);

bool BackspaceAtCursor(char* buffer,
                       int buffer_capacity,
                       int* length,
                       int* cursor);

bool DeleteAtCursor(char* buffer,
                    int buffer_capacity,
                    int* length,
                    int* cursor);

}  // namespace input

