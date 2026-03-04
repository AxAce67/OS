#pragma once

#include <stdint.h>

namespace input {

bool DeleteRange(char* buffer,
                 int buffer_capacity,
                 int* length,
                 int* cursor,
                 int start,
                 int count);

bool InsertByteAtCursor(char* buffer,
                        int buffer_capacity,
                        int* length,
                        int* cursor,
                        int max_length,
                        uint8_t value);

int InsertCStringAtCursor(char* buffer,
                          int buffer_capacity,
                          int* length,
                          int* cursor,
                          int max_length,
                          const char* text);

}  // namespace input

