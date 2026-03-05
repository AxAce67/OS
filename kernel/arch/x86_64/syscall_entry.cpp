#include "arch/x86_64/syscall_entry.hpp"

#include "syscall/syscall.hpp"

namespace {

struct SyscallTrapFrame {
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
} __attribute__((packed));

extern "C" void HandleSyscallTrap(SyscallTrapFrame* frame) {
    if (frame == nullptr) {
        return;
    }
    frame->rax = static_cast<uint64_t>(syscall::Dispatch(frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->rcx));
}

}  // namespace

extern "C" __attribute__((naked)) void IntHandlerSyscallEntry() {
    __asm__ volatile(
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "movq %rsp, %rdi\n"
        "cld\n"
        "callq HandleSyscallTrap\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n");
}

int64_t InvokeSyscallInt80(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret = number;
    __asm__ volatile(
        "int $0x80"
        : "+a"(ret)
        : "D"(arg0), "S"(arg1), "d"(arg2), "c"(arg3)
        : "r8", "r9", "r10", "r11", "memory");
    return static_cast<int64_t>(ret);
}
