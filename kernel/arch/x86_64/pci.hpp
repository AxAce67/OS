#pragma once
#include <stdint.h>

struct PCIAddress {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

struct XHCIControllerInfo {
    bool found;
    PCIAddress address;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint64_t mmio_base;
};

void InitializePCI();
const XHCIControllerInfo& GetXHCIControllerInfo();
