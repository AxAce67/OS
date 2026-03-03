#pragma once
#include <stdint.h>

// I/Oポートへの1バイト出力
static inline void Out8(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

// I/Oポートからの1バイト入力
static inline uint8_t In8(uint16_t port) {
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

// 少し待機する（I/Oポートのアクセス待機用）
static inline void IoWait() {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}
