#pragma once
#include <stdint.h>
#include "arch/x86_64/pci.hpp"

struct XHCICapabilityInfo {
    bool valid;
    uint8_t cap_length;
    uint16_t hci_version;
    uint32_t hcs_params1;
    uint32_t hcc_params1;
    uint32_t db_off;
    uint32_t rts_off;
    uint64_t operational_base;
};

bool ProbeXHCIController(const XHCIControllerInfo& controller, XHCICapabilityInfo* out_info);
