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

struct XHCIPortStatus {
    uint32_t port_id;
    bool connected;
    bool enabled;
    bool over_current;
    bool resetting;
    bool power;
    uint8_t speed;
    uint32_t raw_portsc;
};

bool ProbeXHCIController(const XHCIControllerInfo& controller, XHCICapabilityInfo* out_info);
int XHCIMaxPorts(const XHCICapabilityInfo& info);
int ReadXHCIPortStatus(const XHCICapabilityInfo& info, XHCIPortStatus* ports, int max_ports);
