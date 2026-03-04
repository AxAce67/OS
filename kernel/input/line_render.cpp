#include "input/line_render.hpp"

#include "graphics/console.hpp"
#include "graphics/window.hpp"
#include "input/selection.hpp"

namespace input {

namespace {

int CountU32Digits(unsigned int v) {
    int n = 1;
    while (v >= 10) {
        v /= 10;
        ++n;
    }
    return n;
}

}  // namespace

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
                     int* rendered_len) {
    if (console == nullptr || rendered_len == nullptr || command_buffer == nullptr) {
        return;
    }

    int visual_len = command_len;
    if (ime_enabled && ime_romaji_len > 0) {
        visual_len += ime_romaji_len + 2;
    }
    if (ime_candidate_active && ime_candidate_count > 0) {
        visual_len += 8;
        visual_len += CountU32Digits(static_cast<unsigned int>(ime_candidate_index + 1));
        visual_len += 1;
        visual_len += CountU32Digits(static_cast<unsigned int>(ime_candidate_count));
        visual_len += 1;
    }

    int clear_len = *rendered_len;
    if (clear_len < visual_len) {
        clear_len = visual_len;
    }
    clear_len += 2;
    const int max_clear = console->Columns() - input_col - 1;
    if (clear_len > max_clear) {
        clear_len = max_clear;
    }
    if (clear_len < 0) {
        clear_len = 0;
    }

    console->SetCursorPosition(input_row, input_col);
    for (int i = 0; i < clear_len; ++i) {
        console->Print(" ");
    }
    console->SetCursorPosition(input_row, input_col);
    console->Print(command_buffer);
    if (ime_enabled && ime_romaji_len > 0) {
        console->Print("[");
        console->Print(ime_romaji_buffer);
        console->Print("]");
    }
    if (ime_candidate_active && ime_candidate_count > 0) {
        console->Print(" [cand ");
        console->PrintDec(ime_candidate_index + 1);
        console->Print("/");
        console->PrintDec(ime_candidate_count);
        console->Print("]");
    }

    if (HasSelection(selection_anchor, selection_end)) {
        const int sel_start = SelectionStart(selection_anchor, selection_end);
        const int sel_end = SelectionEnd(selection_anchor, selection_end);
        Window* win = console->RawWindow();
        for (int i = sel_start; i < sel_end; ++i) {
            const int col = input_col + i;
            if (col < input_col || col >= console->Columns()) {
                continue;
            }
            const int px = Console::kMarginX + col * Console::kCellWidth;
            const int py = Console::kMarginY + input_row * Console::kCellHeight;
            win->FillRectangle(px, py, Console::kCellWidth, Console::kCellHeight, {255, 255, 255});
            const char c = (i < command_len) ? command_buffer[i] : ' ';
            win->DrawCharScaled(px, py, c, {0, 0, 0}, Console::kFontScale);
        }
    }

    *rendered_len = visual_len;
    console->SetCursorPosition(input_row, input_col + cursor_pos);
}

}  // namespace input

