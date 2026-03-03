#include "xhci.hpp"

namespace {
uint8_t ReadMMIO8(uint64_t addr) {
    volatile const uint8_t* p = reinterpret_cast<volatile const uint8_t*>(addr);
    return *p;
}

uint16_t ReadMMIO16(uint64_t addr) {
    volatile const uint16_t* p = reinterpret_cast<volatile const uint16_t*>(addr);
    return *p;
}

uint32_t ReadMMIO32(uint64_t addr) {
    volatile const uint32_t* p = reinterpret_cast<volatile const uint32_t*>(addr);
    return *p;
}

void WriteMMIO32(uint64_t addr, uint32_t value) {
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(addr);
    *p = value;
}

uint64_t ReadMMIO64(uint64_t addr) {
    const uint64_t lo = ReadMMIO32(addr);
    const uint64_t hi = ReadMMIO32(addr + 4);
    return lo | (hi << 32);
}
}  // namespace

bool ProbeXHCIController(const XHCIControllerInfo& controller, XHCICapabilityInfo* out_info) {
    if (out_info == nullptr) {
        return false;
    }
    out_info->valid = false;
    if (!controller.found || controller.mmio_base == 0) {
        return false;
    }

    const uint64_t base = controller.mmio_base;
    const uint8_t cap_length = ReadMMIO8(base + 0x00);
    const uint16_t hci_version = ReadMMIO16(base + 0x02);
    const uint32_t hcs_params1 = ReadMMIO32(base + 0x04);
    const uint32_t hcc_params1 = ReadMMIO32(base + 0x10);
    const uint32_t db_off = ReadMMIO32(base + 0x14);
    const uint32_t rts_off = ReadMMIO32(base + 0x18);

    out_info->valid = true;
    out_info->cap_length = cap_length;
    out_info->hci_version = hci_version;
    out_info->hcs_params1 = hcs_params1;
    out_info->hcc_params1 = hcc_params1;
    out_info->db_off = db_off;
    out_info->rts_off = rts_off;
    out_info->operational_base = base + cap_length;
    out_info->max_slots = static_cast<uint8_t>(hcs_params1 & 0xFF);
    out_info->max_interrupters = static_cast<uint8_t>((hcs_params1 >> 8) & 0x7FF);
    out_info->max_ports = static_cast<uint8_t>((hcs_params1 >> 24) & 0xFF);
    out_info->page_size_bitmap = static_cast<uint16_t>(ReadMMIO32(base + 0x08) & 0xFFFF);
    return true;
}

int XHCIMaxPorts(const XHCICapabilityInfo& info) {
    if (!info.valid) {
        return 0;
    }
    return static_cast<int>(info.max_ports);
}

int ReadXHCIPortStatus(const XHCICapabilityInfo& info, XHCIPortStatus* ports, int max_ports) {
    if (!info.valid || ports == nullptr || max_ports <= 0) {
        return 0;
    }
    int hw_ports = XHCIMaxPorts(info);
    if (hw_ports < 0) {
        hw_ports = 0;
    }
    int count = (hw_ports < max_ports) ? hw_ports : max_ports;
    const uint64_t portsc_base = info.operational_base + 0x400;
    for (int i = 0; i < count; ++i) {
        const uint64_t addr = portsc_base + static_cast<uint64_t>(i) * 0x10;
        const uint32_t v = ReadMMIO32(addr);
        ports[i].port_id = static_cast<uint32_t>(i + 1);
        ports[i].connected = (v & (1u << 0)) != 0;
        ports[i].enabled = (v & (1u << 1)) != 0;
        ports[i].over_current = (v & (1u << 3)) != 0;
        ports[i].resetting = (v & (1u << 4)) != 0;
        ports[i].power = (v & (1u << 9)) != 0;
        ports[i].speed = static_cast<uint8_t>((v >> 10) & 0x0F);
        ports[i].raw_portsc = v;
    }
    return count;
}

bool ReadXHCIOperationalStatus(const XHCICapabilityInfo& info, XHCIOperationalStatus* out_status) {
    if (!info.valid || out_status == nullptr) {
        return false;
    }
    const uint64_t op = info.operational_base;
    out_status->valid = true;
    out_status->usbcmd = ReadMMIO32(op + 0x00);
    out_status->usbsts = ReadMMIO32(op + 0x04);
    out_status->dnctrl = ReadMMIO32(op + 0x14);
    out_status->crcr = ReadMMIO64(op + 0x18);
    out_status->dcbaap = ReadMMIO64(op + 0x30);
    out_status->config = ReadMMIO32(op + 0x38);
    out_status->run_stop = (out_status->usbcmd & 0x1u) != 0;
    out_status->hc_halted = (out_status->usbsts & 0x1u) != 0;
    out_status->host_system_error = (out_status->usbsts & (1u << 2)) != 0;
    out_status->event_interrupt = (out_status->usbsts & (1u << 3)) != 0;
    out_status->port_change_detect = (out_status->usbsts & (1u << 4)) != 0;
    return true;
}

bool XHCISetRunStop(const XHCICapabilityInfo& info, bool run, uint32_t timeout_iters) {
    if (!info.valid) {
        return false;
    }
    const uint64_t op = info.operational_base;
    uint32_t cmd = ReadMMIO32(op + 0x00);
    if (run) {
        cmd |= 0x1u;
    } else {
        cmd &= ~0x1u;
    }
    WriteMMIO32(op + 0x00, cmd);

    for (uint32_t i = 0; i < timeout_iters; ++i) {
        const uint32_t sts = ReadMMIO32(op + 0x04);
        const bool halted = (sts & 0x1u) != 0;
        if (run) {
            if (!halted) {
                return true;
            }
        } else {
            if (halted) {
                return true;
            }
        }
    }
    return false;
}

bool XHCIResetController(const XHCICapabilityInfo& info, uint32_t timeout_iters) {
    if (!info.valid) {
        return false;
    }
    const uint64_t op = info.operational_base;

    // xHCI reset is only valid in halted state.
    if (!XHCISetRunStop(info, false, timeout_iters)) {
        return false;
    }

    uint32_t cmd = ReadMMIO32(op + 0x00);
    cmd |= (1u << 1);  // HCRST
    WriteMMIO32(op + 0x00, cmd);

    for (uint32_t i = 0; i < timeout_iters; ++i) {
        const uint32_t cur_cmd = ReadMMIO32(op + 0x00);
        const uint32_t sts = ReadMMIO32(op + 0x04);
        const bool hcrst_set = (cur_cmd & (1u << 1)) != 0;
        const bool halted = (sts & 0x1u) != 0;
        if (!hcrst_set && halted) {
            return true;
        }
    }
    return false;
}
