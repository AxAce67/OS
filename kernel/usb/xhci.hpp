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
    uint8_t max_slots;
    uint16_t max_interrupters;
    uint8_t max_ports;
    uint16_t page_size_bitmap;
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

struct XHCIOperationalStatus {
    bool valid;
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t dnctrl;
    uint64_t crcr;
    uint64_t dcbaap;
    uint32_t config;
    bool run_stop;
    bool hc_halted;
    bool host_system_error;
    bool event_interrupt;
    bool port_change_detect;
};

struct XHCICommandResult {
    bool ok;
    uint8_t completion_code;
    uint8_t slot_id;
    uint8_t trb_type;
};

struct XHCIAddressDeviceResult {
    bool ok;
    uint8_t completion_code;
    uint8_t slot_id;
};

struct XHCIConfigureEndpointResult {
    bool ok;
    uint8_t completion_code;
    uint8_t slot_id;
};

struct XHCIControlTransferResult {
    bool ok;
    uint8_t completion_code;
    uint8_t slot_id;
    uint8_t endpoint_id;
};

struct XHCIInterruptEndpointInfo {
    bool ok;
    uint8_t configuration_value;
    uint8_t endpoint_address;
    uint8_t endpoint_id;
    uint16_t max_packet_size;
    uint8_t interval;
};

struct XHCIInterruptInResult {
    bool ok;
    uint8_t completion_code;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint32_t transfer_length;
    uint32_t data_length;
    uint8_t data[64];
};

bool ProbeXHCIController(const XHCIControllerInfo& controller, XHCICapabilityInfo* out_info);
int XHCIMaxPorts(const XHCICapabilityInfo& info);
int ReadXHCIPortStatus(const XHCICapabilityInfo& info, XHCIPortStatus* ports, int max_ports);
bool ReadXHCIOperationalStatus(const XHCICapabilityInfo& info, XHCIOperationalStatus* out_status);
bool XHCISetRunStop(const XHCICapabilityInfo& info, bool run, uint32_t timeout_iters = 1000000);
bool XHCIResetController(const XHCICapabilityInfo& info, uint32_t timeout_iters = 2000000);
bool XHCIInitializeCommandAndEventRings(const XHCICapabilityInfo& info);
bool XHCIEnableSlot(const XHCICapabilityInfo& info, XHCICommandResult* out_result, uint32_t timeout_iters = 3000000);
bool XHCIAddressDevice(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t root_port, uint8_t port_speed, XHCIAddressDeviceResult* out_result, uint32_t timeout_iters = 3000000);
bool XHCISetConfiguration(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t configuration_value, XHCIControlTransferResult* out_result, uint32_t timeout_iters = 3000000);
bool XHCIFindFirstInterruptInEndpoint(const XHCICapabilityInfo& info, uint8_t slot_id, XHCIInterruptEndpointInfo* out_info, uint32_t timeout_iters = 3000000);
bool XHCIConfigureInterruptInEndpointEx(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t endpoint_id, uint16_t max_packet_size, uint8_t interval, XHCIConfigureEndpointResult* out_result, uint32_t timeout_iters = 3000000);
bool XHCIConfigureInterruptInEndpoint(const XHCICapabilityInfo& info, uint8_t slot_id, uint16_t max_packet_size, uint8_t interval, XHCIConfigureEndpointResult* out_result, uint32_t timeout_iters = 3000000);
bool XHCIPollInterruptIn(const XHCICapabilityInfo& info, uint8_t slot_id, uint32_t request_length, XHCIInterruptInResult* out_result, uint32_t timeout_iters = 3000000);
