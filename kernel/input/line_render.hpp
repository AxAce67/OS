#pragma once

class Console;

namespace input {

void RenderInputLine(Console* console,
                     int input_row,
                     int input_col,
                     const char* command_buffer,
                     int command_len,
                     int cursor_pos,
                     bool ime_enabled,
                     const char* ime_romaji_buffer,
                     int ime_romaji_len,
                     bool ime_candidate_active,
                     int ime_candidate_index,
                     int ime_candidate_count,
                     int selection_anchor,
                     int selection_end,
                     int* rendered_len);

}  // namespace input

