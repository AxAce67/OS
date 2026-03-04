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

}  // namespace input

