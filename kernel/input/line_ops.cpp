#include "input/line_ops.hpp"

namespace input {

bool DeleteRange(char* buffer,
                 int buffer_capacity,
                 int* length,
                 int* cursor,
                 int start,
                 int count) {
    if (buffer == nullptr || length == nullptr || cursor == nullptr || buffer_capacity <= 0) {
        return false;
    }
    if (count <= 0 || start < 0 || start > *length) {
        return false;
    }
    if (start + count > *length) {
        count = *length - start;
    }
    for (int i = start; i + count <= *length; ++i) {
        buffer[i] = buffer[i + count];
    }
    *length -= count;
    if (*length < 0) {
        *length = 0;
    }
    if (*cursor > *length) {
        *cursor = *length;
    }
    if (*length >= buffer_capacity) {
        *length = buffer_capacity - 1;
    }
    buffer[*length] = '\0';
    return true;
}

bool InsertByteAtCursor(char* buffer,
                        int buffer_capacity,
                        int* length,
                        int* cursor,
                        int max_length,
                        uint8_t value) {
    if (buffer == nullptr || length == nullptr || cursor == nullptr || buffer_capacity <= 1) {
        return false;
    }
    if (*length < 0) {
        *length = 0;
    }
    if (*cursor < 0) {
        *cursor = 0;
    }
    if (*cursor > *length) {
        *cursor = *length;
    }
    if (max_length > buffer_capacity - 1) {
        max_length = buffer_capacity - 1;
    }
    if (*length >= max_length) {
        return false;
    }
    for (int i = *length; i > *cursor; --i) {
        buffer[i] = buffer[i - 1];
    }
    buffer[*cursor] = static_cast<char>(value);
    ++(*length);
    ++(*cursor);
    buffer[*length] = '\0';
    return true;
}

int InsertCStringAtCursor(char* buffer,
                          int buffer_capacity,
                          int* length,
                          int* cursor,
                          int max_length,
                          const char* text) {
    if (text == nullptr) {
        return 0;
    }
    int inserted = 0;
    for (int i = 0; text[i] != '\0'; ++i) {
        if (!InsertByteAtCursor(buffer, buffer_capacity, length, cursor, max_length,
                                static_cast<uint8_t>(text[i]))) {
            break;
        }
        ++inserted;
    }
    return inserted;
}

}  // namespace input

