#include "pci.hpp"
#include "io.hpp"

namespace {
const uint16_t kPCIConfigAddressPort = 0xCF8;
const uint16_t kPCIConfigDataPort = 0xCFC;

XHCIControllerInfo g_xhci = {};

uint32_t MakeConfigAddress(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg_offset) {
    return (1u << 31) |
           (static_cast<uint32_t>(bus) << 16) |
           (static_cast<uint32_t>(device) << 11) |
           (static_cast<uint32_t>(function) << 8) |
           (reg_offset & 0xFC);
}

uint32_t ReadConfig32(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg_offset) {
    Out32(kPCIConfigAddressPort, MakeConfigAddress(bus, device, function, reg_offset));
    return In32(kPCIConfigDataPort);
}

uint16_t ReadVendorId(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint16_t>(ReadConfig32(bus, device, function, 0x00) & 0xFFFF);
}

uint8_t ReadHeaderType(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint8_t>((ReadConfig32(bus, device, function, 0x0C) >> 16) & 0xFF);
}

uint8_t ReadClassCode(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint8_t>((ReadConfig32(bus, device, function, 0x08) >> 24) & 0xFF);
}

uint8_t ReadSubclass(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint8_t>((ReadConfig32(bus, device, function, 0x08) >> 16) & 0xFF);
}

uint8_t ReadProgIf(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint8_t>((ReadConfig32(bus, device, function, 0x08) >> 8) & 0xFF);
}

uint16_t ReadDeviceId(uint8_t bus, uint8_t device, uint8_t function) {
    return static_cast<uint16_t>((ReadConfig32(bus, device, function, 0x00) >> 16) & 0xFFFF);
}

uint32_t ReadBAR(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_index) {
    const uint8_t reg = static_cast<uint8_t>(0x10 + bar_index * 4);
    return ReadConfig32(bus, device, function, reg);
}

uint64_t ReadMMIOBase(uint8_t bus, uint8_t device, uint8_t function) {
    const uint32_t bar0 = ReadBAR(bus, device, function, 0);
    if ((bar0 & 0x1u) != 0) {
        return 0; // I/O BAR
    }
    const uint32_t type = (bar0 >> 1) & 0x3u;
    if (type == 0x2u) {
        const uint32_t bar1 = ReadBAR(bus, device, function, 1);
        const uint64_t low = static_cast<uint64_t>(bar0 & ~0x0Fu);
        const uint64_t high = static_cast<uint64_t>(bar1);
        return (high << 32) | low;
    }
    return static_cast<uint64_t>(bar0 & ~0x0Fu);
}

void TryRegisterXHCI(uint8_t bus, uint8_t device, uint8_t function) {
    if (g_xhci.found) {
        return;
    }
    const uint8_t class_code = ReadClassCode(bus, device, function);
    const uint8_t subclass = ReadSubclass(bus, device, function);
    const uint8_t prog_if = ReadProgIf(bus, device, function);
    if (class_code != 0x0C || subclass != 0x03 || prog_if != 0x30) {
        return;
    }

    g_xhci.found = true;
    g_xhci.address = {bus, device, function};
    g_xhci.vendor_id = ReadVendorId(bus, device, function);
    g_xhci.device_id = ReadDeviceId(bus, device, function);
    g_xhci.class_code = class_code;
    g_xhci.subclass = subclass;
    g_xhci.prog_if = prog_if;
    g_xhci.mmio_base = ReadMMIOBase(bus, device, function);
}
}  // namespace

extern "C" void* memset(void* s, int c, uint64_t n) {
    uint8_t* p = reinterpret_cast<uint8_t*>(s);
    while (n--) {
        *p++ = static_cast<uint8_t>(c);
    }
    return s;
}

void InitializePCI() {
    g_xhci = {};
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            const uint16_t vendor0 = ReadVendorId(static_cast<uint8_t>(bus), device, 0);
            if (vendor0 == 0xFFFF) {
                continue;
            }

            const uint8_t header = ReadHeaderType(static_cast<uint8_t>(bus), device, 0);
            const uint8_t function_count = (header & 0x80) ? 8 : 1;
            for (uint8_t function = 0; function < function_count; ++function) {
                const uint16_t vendor = ReadVendorId(static_cast<uint8_t>(bus), device, function);
                if (vendor == 0xFFFF) {
                    continue;
                }
                TryRegisterXHCI(static_cast<uint8_t>(bus), device, function);
            }
        }
    }
}

const XHCIControllerInfo& GetXHCIControllerInfo() {
    return g_xhci;
}
