#include "timer.hpp"
#include "apic.hpp"

namespace {
constexpr uint32_t kIA32ApicBaseMsr = 0x1B;
constexpr uint64_t kIA32ApicBaseMask = 0xFFFFF000ULL;

constexpr uint32_t kLapicRegTimerLVT = 0x320;
constexpr uint32_t kLapicRegTimerInitialCount = 0x380;
constexpr uint32_t kLapicRegTimerDivide = 0x3E0;
constexpr uint32_t kLapicTimerVector = 0x40;
constexpr uint32_t kLapicTimerPeriodic = 1U << 17;
constexpr uint32_t kLapicDivideBy16 = 0x3;
constexpr uint32_t kLapicInitialCount = 10'000'000;

volatile uint64_t timer_tick = 0;

uint64_t ReadMSR(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline void WriteLAPIC(uint32_t reg, uint32_t value) {
    uint64_t base = ReadMSR(kIA32ApicBaseMsr) & kIA32ApicBaseMask;
    *reinterpret_cast<volatile uint32_t*>(base + reg) = value;
}
}

void InitializeLAPICTimer() {
    WriteLAPIC(kLapicRegTimerDivide, kLapicDivideBy16);
    WriteLAPIC(kLapicRegTimerLVT, kLapicTimerPeriodic | kLapicTimerVector);
    WriteLAPIC(kLapicRegTimerInitialCount, kLapicInitialCount);
}

void NotifyTimerTick() {
    ++timer_tick;
}

uint64_t CurrentTick() {
    return timer_tick;
}

