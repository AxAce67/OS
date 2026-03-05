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

extern "C" volatile uint64_t g_ring3_saved_rsp = 0;
extern "C" volatile uint64_t g_ring3_resume_rip = 0;
extern "C" volatile uint8_t g_ring3_active = 0;

constexpr uint16_t kKernelCodeSelector = 0x08;
constexpr uint16_t kUserCodeSelector = 0x23;
constexpr uint16_t kUserDataSelector = 0x1B;

extern "C" void HandleSyscallTrap(SyscallTrapFrame* frame) {
    if (frame == nullptr) {
        return;
    }
    const uint64_t number = frame->rax;
    const bool from_user = (frame->cs & 0x3) == 0x3;
    const int64_t ret =
        syscall::DispatchFromTrap(number, frame->rdi, frame->rsi, frame->rdx, frame->rcx, from_user);

    if (from_user &&
        number == static_cast<uint64_t>(syscall::Number::kExitToKernel) &&
        g_ring3_active != 0 &&
        g_ring3_resume_rip != 0) {
        frame->rip = g_ring3_resume_rip;
        frame->cs = kKernelCodeSelector;
        frame->rflags |= (1ULL << 9);
    }
    frame->rax = static_cast<uint64_t>(ret);
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

int RunUserModeFunction(uint64_t entry_rip, uint64_t user_rsp) {
    __asm__ volatile(
        "movq %%rsp, g_ring3_saved_rsp(%%rip)\n"
        "lea 1f(%%rip), %%rax\n"
        "movq %%rax, g_ring3_resume_rip(%%rip)\n"
        "movb $1, g_ring3_active(%%rip)\n"
        "pushq %[user_ss]\n"
        "pushq %[user_rsp]\n"
        "pushfq\n"
        "orq $0x200, (%%rsp)\n"
        "pushq %[user_cs]\n"
        "pushq %[entry]\n"
        "iretq\n"
        "1:\n"
        "movb $0, g_ring3_active(%%rip)\n"
        "movq g_ring3_saved_rsp(%%rip), %%rsp\n"
        :
        : [entry] "r"(entry_rip),
          [user_rsp] "r"(user_rsp),
          [user_cs] "i"(kUserCodeSelector),
          [user_ss] "i"(kUserDataSelector)
        : "rax", "memory");
    return 0;
}
