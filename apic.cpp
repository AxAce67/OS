#include "apic.hpp"

namespace {
constexpr uint32_t kIA32ApicBaseMsr = 0x1B;
constexpr uint64_t kIA32ApicBaseMsrEnable = 1ULL << 11;
constexpr uint64_t kIA32ApicBaseMask = 0xFFFFF000ULL;

constexpr uint32_t kLapicRegEOI = 0x0B0;
constexpr uint32_t kLapicRegSVR = 0x0F0;
constexpr uint32_t kSpuriousVector = 0xFF;
constexpr uint32_t kSVREnable = 1U << 8;

volatile uint32_t* lapic_base = nullptr;

uint64_t ReadMSR(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

void WriteMSR(uint32_t msr, uint64_t value) {
    uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFF);
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

inline void WriteLAPIC(uint32_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uint64_t>(lapic_base) + reg) = value;
}
}

void InitializeLocalAPIC() {
    uint64_t apic_base_msr = ReadMSR(kIA32ApicBaseMsr);
    apic_base_msr |= kIA32ApicBaseMsrEnable;
    WriteMSR(kIA32ApicBaseMsr, apic_base_msr);

    uint64_t base = apic_base_msr & kIA32ApicBaseMask;
    lapic_base = reinterpret_cast<volatile uint32_t*>(base);

    WriteLAPIC(kLapicRegSVR, kSVREnable | kSpuriousVector);
}

void NotifyLocalAPICEOI() {
    if (lapic_base == nullptr) {
        return;
    }
    WriteLAPIC(kLapicRegEOI, 0);
}

