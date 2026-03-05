#pragma once

#include "input/message.hpp"
#include "input/runtime_input_flow.hpp"

namespace input::entry {

template <class TBuildState, class TDispatch>
inline void HandleMouseMessage(TBuildState&& build_state,
                               TDispatch&& dispatch,
                               const Message& msg) {
    RuntimeMouseMessageContext mouse_context{};
    RuntimeMouseWindowGeometry geometry{};
    RuntimeConsoleGridMetrics console_metrics{};
    RuntimeMouseConsoleSelectionRefs selection_refs{};
    build_state(&mouse_context, &geometry, &console_metrics, &selection_refs);
    dispatch(msg, mouse_context, geometry, console_metrics, selection_refs);
}

template <class TCandidateEntry, class TBuildState, class TDispatch>
inline void HandleKeyboardMessage(TBuildState&& build_state,
                                  TDispatch&& dispatch,
                                  const Message& msg) {
    RuntimeEnterCommandRefs enter_refs{};
    RuntimeCommandInputStateRefs reset_refs{};
    RuntimeKeyboardDecodeRefs keyboard_decode_refs{};
    RuntimeImeProcessContextT<TCandidateEntry> ime_process_context{};
    RuntimeKeyboardMessageContextT<TCandidateEntry> keyboard_context{};
    build_state(&enter_refs,
                &reset_refs,
                &keyboard_decode_refs,
                &ime_process_context,
                &keyboard_context);
    dispatch(msg.keycode, keyboard_context, enter_refs, reset_refs);
}

}  // namespace input::entry

