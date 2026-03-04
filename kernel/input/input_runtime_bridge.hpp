#pragma once

#include "input/runtime_input_flow.hpp"

namespace input::bridge {

inline void BuildMouseRuntimeContext(RuntimeMouseMessageContext* out_context,
                                     uint8_t* current_buttons,
                                     uint64_t* left_press_count,
                                     uint64_t* right_press_count,
                                     uint64_t* middle_press_count,
                                     int* pointer_logical_x,
                                     int* pointer_logical_y,
                                     int screen_w,
                                     int screen_h,
                                     bool xhci_hid_auto_enabled,
                                     uint64_t* last_absolute_mouse_tick,
                                     bool* pointer_visual_dirty,
                                     uint64_t* last_pointer_move_tick,
                                     int* dragging_window,
                                     int* drag_offset_x,
                                     int* drag_offset_y,
                                     bool* selecting_with_mouse,
                                     int* drag_pending_window,
                                     int* drag_pending_x,
                                     int* drag_pending_y,
                                     bool* drag_pending_move,
                                     bool* drag_visual_dirty) {
    BuildRuntimeMouseMessageContext(
        out_context,
        RuntimeMouseButtonCounterRefs{
            current_buttons,
            left_press_count,
            right_press_count,
            middle_press_count,
        },
        RuntimeMousePointerUpdateRefs{
            pointer_logical_x,
            pointer_logical_y,
            screen_w,
            screen_h,
            xhci_hid_auto_enabled,
            last_absolute_mouse_tick,
            pointer_visual_dirty,
            last_pointer_move_tick,
        },
        RuntimeMouseDragRefs{
            dragging_window,
            drag_offset_x,
            drag_offset_y,
        },
        RuntimeMouseDragStopRefs{
            selecting_with_mouse,
            dragging_window,
        },
        RuntimePendingDragRefs{
            drag_pending_window,
            drag_pending_x,
            drag_pending_y,
            drag_pending_move,
            drag_visual_dirty,
        });
}

inline void BuildMouseWindowGeometry(RuntimeMouseWindowGeometry* out_geometry,
                                     int term_frame_x,
                                     int term_frame_y,
                                     int term_frame_w,
                                     int term_frame_h,
                                     int term_title_h,
                                     int info_frame_x,
                                     int info_frame_y,
                                     int info_frame_w,
                                     int info_frame_h,
                                     int info_title_h,
                                     int term_drag_max_x,
                                     int term_drag_max_y,
                                     int info_drag_max_x,
                                     int info_drag_max_y,
                                     int term_current_x,
                                     int term_current_y,
                                     int info_current_x,
                                     int info_current_y) {
    if (out_geometry == nullptr) {
        return;
    }
    out_geometry->term_frame_x = term_frame_x;
    out_geometry->term_frame_y = term_frame_y;
    out_geometry->term_frame_w = term_frame_w;
    out_geometry->term_frame_h = term_frame_h;
    out_geometry->term_title_h = term_title_h;
    out_geometry->info_frame_x = info_frame_x;
    out_geometry->info_frame_y = info_frame_y;
    out_geometry->info_frame_w = info_frame_w;
    out_geometry->info_frame_h = info_frame_h;
    out_geometry->info_title_h = info_title_h;
    out_geometry->term_drag_max_x = term_drag_max_x;
    out_geometry->term_drag_max_y = term_drag_max_y;
    out_geometry->info_drag_max_x = info_drag_max_x;
    out_geometry->info_drag_max_y = info_drag_max_y;
    out_geometry->term_current_x = term_current_x;
    out_geometry->term_current_y = term_current_y;
    out_geometry->info_current_x = info_current_x;
    out_geometry->info_current_y = info_current_y;
}

inline void BuildConsoleGridMetrics(RuntimeConsoleGridMetrics* out_metrics,
                                    int origin_x,
                                    int origin_y,
                                    int width,
                                    int height,
                                    int margin_x,
                                    int margin_y,
                                    int cell_w,
                                    int cell_h) {
    if (out_metrics == nullptr) {
        return;
    }
    out_metrics->origin_x = origin_x;
    out_metrics->origin_y = origin_y;
    out_metrics->width = width;
    out_metrics->height = height;
    out_metrics->margin_x = margin_x;
    out_metrics->margin_y = margin_y;
    out_metrics->cell_w = cell_w;
    out_metrics->cell_h = cell_h;
}

inline void BuildMouseSelectionRefs(RuntimeMouseConsoleSelectionRefs* out_refs,
                                    bool* selecting_with_mouse,
                                    int* selection_anchor,
                                    int* selection_end,
                                    int* cursor_pos,
                                    int input_row,
                                    int input_col,
                                    int command_len) {
    if (out_refs == nullptr) {
        return;
    }
    out_refs->selecting_with_mouse = selecting_with_mouse;
    out_refs->selection_anchor = selection_anchor;
    out_refs->selection_end = selection_end;
    out_refs->cursor_pos = cursor_pos;
    out_refs->input_row = input_row;
    out_refs->input_col = input_col;
    out_refs->command_len = command_len;
}

inline void BuildKeyboardDecodeRefs(RuntimeKeyboardDecodeRefs* out_decode_refs,
                                    uint64_t* irq_count,
                                    uint8_t* last_raw,
                                    uint8_t* last_key,
                                    bool* last_extended,
                                    bool* last_released,
                                    bool* e0_prefix,
                                    KeyboardModifiers* modifiers) {
    if (out_decode_refs == nullptr) {
        return;
    }
    out_decode_refs->irq_count = irq_count;
    out_decode_refs->last_raw = last_raw;
    out_decode_refs->last_key = last_key;
    out_decode_refs->last_extended = last_extended;
    out_decode_refs->last_released = last_released;
    out_decode_refs->e0_prefix = e0_prefix;
    out_decode_refs->modifiers = modifiers;
}

template <class TCandidateEntry>
inline void BuildKeyboardImeProcessContext(RuntimeImeProcessContextT<TCandidateEntry>* out_context,
                                           char* romaji_buffer,
                                           int romaji_capacity,
                                           int* romaji_len,
                                           const TCandidateEntry** candidate_entry) {
    if (out_context == nullptr) {
        return;
    }
    out_context->romaji_buffer = romaji_buffer;
    out_context->romaji_capacity = romaji_capacity;
    out_context->romaji_len = romaji_len;
    out_context->candidate_entry = candidate_entry;
}

template <class TCandidateEntry>
inline void BuildKeyboardRuntimeContext(RuntimeKeyboardMessageContextT<TCandidateEntry>* out_context,
                                        const RuntimeKeyboardDecodeRefs& decode_refs,
                                        bool* key_down_extended,
                                        bool* key_down_normal,
                                        bool key_repeat_enabled,
                                        bool jp_layout,
                                        bool ime_enabled,
                                        bool has_halfwidth_kana_font,
                                        bool ime_candidate_active,
                                        const TCandidateEntry* ime_candidate_entry,
                                        int ime_romaji_len,
                                        const RuntimeImeProcessContextT<TCandidateEntry>& ime_process_context) {
    if (out_context == nullptr) {
        return;
    }
    out_context->decode_refs.irq_count = decode_refs.irq_count;
    out_context->decode_refs.last_raw = decode_refs.last_raw;
    out_context->decode_refs.last_key = decode_refs.last_key;
    out_context->decode_refs.last_extended = decode_refs.last_extended;
    out_context->decode_refs.last_released = decode_refs.last_released;
    out_context->decode_refs.e0_prefix = decode_refs.e0_prefix;
    out_context->decode_refs.modifiers = decode_refs.modifiers;
    out_context->key_down_refs.key_down_extended = key_down_extended;
    out_context->key_down_refs.key_down_normal = key_down_normal;
    out_context->key_repeat_enabled = key_repeat_enabled;
    out_context->jp_layout = jp_layout;
    out_context->ime_enabled = ime_enabled;
    out_context->has_halfwidth_kana_font = has_halfwidth_kana_font;
    out_context->ime_candidate_active = ime_candidate_active;
    out_context->ime_candidate_entry = ime_candidate_entry;
    out_context->ime_romaji_len = ime_romaji_len;
    out_context->ime_process_context.romaji_buffer = ime_process_context.romaji_buffer;
    out_context->ime_process_context.romaji_capacity = ime_process_context.romaji_capacity;
    out_context->ime_process_context.romaji_len = ime_process_context.romaji_len;
    out_context->ime_process_context.candidate_entry = ime_process_context.candidate_entry;
}

inline void BuildEnterCommandRefs(RuntimeEnterCommandRefs* out_refs,
                                  char* command_buffer,
                                  int command_capacity,
                                  int* command_len) {
    if (out_refs == nullptr) {
        return;
    }
    out_refs->command_buffer = command_buffer;
    out_refs->command_capacity = command_capacity;
    out_refs->command_len = command_len;
}

inline void BuildCommandResetRefs(RuntimeCommandInputStateRefs* out_refs,
                                  char* command_buffer,
                                  int command_capacity,
                                  int* command_len,
                                  int* cursor_pos,
                                  int* rendered_len,
                                  char* ime_romaji_buffer,
                                  int ime_romaji_capacity,
                                  int* ime_romaji_len) {
    if (out_refs == nullptr) {
        return;
    }
    out_refs->command_buffer = command_buffer;
    out_refs->command_capacity = command_capacity;
    out_refs->command_len = command_len;
    out_refs->cursor_pos = cursor_pos;
    out_refs->rendered_len = rendered_len;
    out_refs->ime_romaji_buffer = ime_romaji_buffer;
    out_refs->ime_romaji_capacity = ime_romaji_capacity;
    out_refs->ime_romaji_len = ime_romaji_len;
}

}  // namespace input::bridge
