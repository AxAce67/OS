#pragma once

#include <stdint.h>

enum class Ring3ReturnReason : uint8_t {
    kNone = 0,
    kExit = 1,
    kYield = 2,
};

struct Ring3SyscallFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

int64_t InvokeSyscallInt80(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int RunUserModeFunction(uint64_t entry_rip, uint64_t user_rsp);
int RunUserModeFunctionWithArgs(uint64_t entry_rip, uint64_t user_rsp, uint64_t arg0, uint64_t arg1, uint64_t arg2);
int ResumeUserModeFrame(const Ring3SyscallFrame* frame);
int64_t GetLastRing3ExitCode();
Ring3ReturnReason GetLastRing3ReturnReason();
