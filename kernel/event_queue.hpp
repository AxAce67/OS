#pragma once

#include "queue.hpp"
#include "input/message.hpp"

// Single-CPU phase:
// - IRQ side pushes events quickly.
// - Kernel main loop pops under interrupt masking.
// This API centralizes queue accesses so SMP migration can replace internals
// (e.g. spinlock/atomic ring) without touching call sites.
namespace event_queue {

inline bool PushFromInterrupt(ArrayQueue<Message, 256>* queue, const Message& msg) {
    if (queue == nullptr) {
        return false;
    }
    return queue->Push(msg);
}

inline bool Peek(ArrayQueue<Message, 256>* queue, Message* out_msg) {
    if (queue == nullptr || out_msg == nullptr) {
        return false;
    }
    return queue->Peek(*out_msg);
}

inline bool Pop(ArrayQueue<Message, 256>* queue, Message* out_msg) {
    if (queue == nullptr || out_msg == nullptr) {
        return false;
    }
    return queue->Pop(*out_msg);
}

inline int Count(ArrayQueue<Message, 256>* queue) {
    if (queue == nullptr) {
        return 0;
    }
    return queue->Count();
}

}  // namespace event_queue

