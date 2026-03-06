#include "arch/x86_64/syscall_entry.hpp"

#include "proc/process.hpp"
#include "syscall/syscall.hpp"

namespace {

extern "C" volatile uint64_t g_ring3_saved_rsp = 0;
extern "C" volatile uint64_t g_ring3_resume_rip = 0;
extern "C" volatile uint8_t g_ring3_active = 0;
extern "C" volatile uint8_t g_ring3_return_requested = 0;
extern "C" volatile int64_t g_ring3_last_exit_code = 0;
extern "C" volatile uint8_t g_ring3_last_return_reason = 0;

constexpr uint16_t kUserCodeSelector = 0x23;
constexpr uint16_t kUserDataSelector = 0x1B;

extern "C" void HandleSyscallTrap(Ring3SyscallFrame* frame) {
    if (frame == nullptr) {
        return;
    }
    const uint64_t number = frame->rax;
    const bool from_user = (frame->cs & 0x3) == 0x3;
    const int64_t ret =
        syscall::DispatchFromTrap(number, frame->rdi, frame->rsi, frame->rdx, frame->rcx, from_user);

    if (from_user &&
        g_ring3_active != 0 &&
        g_ring3_resume_rip != 0) {
        if (number == static_cast<uint64_t>(syscall::Number::kExitToKernel)) {
            g_ring3_last_exit_code = static_cast<int64_t>(frame->rdi);
            g_ring3_last_return_reason = static_cast<uint8_t>(Ring3ReturnReason::kExit);
            g_ring3_return_requested = 1;
        } else if (number == static_cast<uint64_t>(syscall::Number::kYield)) {
            proc::SaveCurrentProcessUserFrame(frame);
            g_ring3_last_return_reason = static_cast<uint8_t>(Ring3ReturnReason::kYield);
            g_ring3_return_requested = 1;
        }
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
        "cmpb $0, g_ring3_return_requested(%rip)\n"
        "je 1f\n"
        "movb $0, g_ring3_return_requested(%rip)\n"
        "movb $0, g_ring3_active(%rip)\n"
        "movq g_ring3_saved_rsp(%rip), %rsp\n"
        "movq g_ring3_resume_rip(%rip), %rax\n"
        "jmp *%rax\n"
        "1:\n"
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
    return RunUserModeFunctionWithArgs(entry_rip, user_rsp, 0, 0, 0);
}

int RunUserModeFunctionWithArgs(uint64_t entry_rip, uint64_t user_rsp, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    g_ring3_last_return_reason = static_cast<uint8_t>(Ring3ReturnReason::kNone);
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
          [arg0] "D"(arg0),
          [arg1] "S"(arg1),
          [arg2] "d"(arg2),
          [user_cs] "i"(kUserCodeSelector),
          [user_ss] "i"(kUserDataSelector)
        : "rax", "memory");
    return 0;
}

int ResumeUserModeFrame(const Ring3SyscallFrame* frame) {
    if (frame == nullptr) {
        return -1;
    }
    g_ring3_last_return_reason = static_cast<uint8_t>(Ring3ReturnReason::kNone);
    __asm__ volatile(
        "movq %%rsp, g_ring3_saved_rsp(%%rip)\n"
        "lea 1f(%%rip), %%rax\n"
        "movq %%rax, g_ring3_resume_rip(%%rip)\n"
        "movb $1, g_ring3_active(%%rip)\n"
        "movq %[frame], %%rsp\n"
        "popq %%r15\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rbp\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rbx\n"
        "popq %%rax\n"
        "iretq\n"
        "1:\n"
        "movb $0, g_ring3_active(%%rip)\n"
        "movq g_ring3_saved_rsp(%%rip), %%rsp\n"
        :
        : [frame] "r"(frame)
        : "rax", "memory");
    return 0;
}

int64_t GetLastRing3ExitCode() {
    return g_ring3_last_exit_code;
}

Ring3ReturnReason GetLastRing3ReturnReason() {
    return static_cast<Ring3ReturnReason>(g_ring3_last_return_reason);
}
