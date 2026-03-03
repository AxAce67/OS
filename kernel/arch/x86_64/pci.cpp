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

uint32_t ReadBAR0(uint8_t bus, uint8_t device, uint8_t function) {
    return ReadConfig32(bus, device, function, 0x10);
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
    const uint32_t bar0 = ReadBAR0(bus, device, function);
    g_xhci.mmio_base = bar0 & ~0x0Fu;
}
}  // namespace

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
