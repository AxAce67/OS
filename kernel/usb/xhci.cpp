#include "xhci.hpp"

namespace {
struct alignas(16) TRB {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
};

struct alignas(64) EventRingSegmentTableEntry {
    uint64_t ring_segment_base;
    uint16_t ring_segment_size;
    uint16_t rsvd0;
    uint32_t rsvd1;
};

alignas(64) uint64_t g_dcbaa[256];
alignas(64) TRB g_command_ring[256];
alignas(64) TRB g_event_ring[256];
alignas(64) EventRingSegmentTableEntry g_erst[1];
bool g_rings_initialized = false;
uint32_t g_command_enqueue_index = 0;
uint8_t g_command_cycle_bit = 1;
uint32_t g_event_dequeue_index = 0;
uint8_t g_event_cycle_bit = 1;

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

void WriteMMIO64(uint64_t addr, uint64_t value) {
    WriteMMIO32(addr, static_cast<uint32_t>(value & 0xFFFFFFFFu));
    WriteMMIO32(addr + 4, static_cast<uint32_t>(value >> 32));
}

uint64_t ReadMMIO64(uint64_t addr) {
    const uint64_t lo = ReadMMIO32(addr);
    const uint64_t hi = ReadMMIO32(addr + 4);
    return lo | (hi << 32);
}

void MemorySet(void* p, uint8_t v, uint32_t n) {
    uint8_t* b = reinterpret_cast<uint8_t*>(p);
    for (uint32_t i = 0; i < n; ++i) {
        b[i] = v;
    }
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
    out_info->max_interrupters = static_cast<uint16_t>((hcs_params1 >> 8) & 0x7FF);
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

bool XHCIInitializeCommandAndEventRings(const XHCICapabilityInfo& info) {
    if (!info.valid) {
        return false;
    }

    // Reset first so controller is in a known state.
    if (!XHCIResetController(info)) {
        return false;
    }

    MemorySet(g_dcbaa, 0, sizeof(g_dcbaa));
    MemorySet(g_command_ring, 0, sizeof(g_command_ring));
    MemorySet(g_event_ring, 0, sizeof(g_event_ring));
    MemorySet(g_erst, 0, sizeof(g_erst));

    // Command ring link TRB at last entry.
    TRB& link = g_command_ring[255];
    const uint64_t ring_base = reinterpret_cast<uint64_t>(&g_command_ring[0]);
    link.dword0 = static_cast<uint32_t>(ring_base & 0xFFFFFFFFu);
    link.dword1 = static_cast<uint32_t>(ring_base >> 32);
    link.dword2 = 0;
    // Type=Link(6), Toggle Cycle=1.
    link.dword3 = (6u << 10) | (1u << 1);

    g_erst[0].ring_segment_base = reinterpret_cast<uint64_t>(&g_event_ring[0]);
    g_erst[0].ring_segment_size = 256;

    const uint64_t op = info.operational_base;
    const uint64_t runtime = (reinterpret_cast<uint64_t>(info.operational_base) - info.cap_length) + (info.rts_off & ~0x1Fu);
    const uint64_t intr0 = runtime + 0x20;

    // Program DCBAAP and command ring.
    WriteMMIO64(op + 0x30, reinterpret_cast<uint64_t>(&g_dcbaa[0]));
    WriteMMIO64(op + 0x18, ring_base | 1u);  // RCS=1

    // Program event ring for interrupter 0.
    WriteMMIO32(intr0 + 0x08, 1);  // ERSTSZ
    WriteMMIO64(intr0 + 0x10, reinterpret_cast<uint64_t>(&g_erst[0]));  // ERSTBA
    WriteMMIO64(intr0 + 0x18, reinterpret_cast<uint64_t>(&g_event_ring[0]));  // ERDP

    // Set MaxSlotsEn (lower 8 bits in CONFIG)
    uint32_t config = ReadMMIO32(op + 0x38);
    config &= ~0xFFu;
    config |= (info.max_slots == 0 ? 1 : info.max_slots);
    WriteMMIO32(op + 0x38, config);

    g_command_enqueue_index = 0;
    g_command_cycle_bit = 1;
    g_event_dequeue_index = 0;
    g_event_cycle_bit = 1;
    g_rings_initialized = true;

    // Start controller so command/event path is active.
    return XHCISetRunStop(info, true);
}

bool XHCIEnableSlot(const XHCICapabilityInfo& info, XHCICommandResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = 0;
    out_result->trb_type = 0;

    if (!info.valid) {
        return false;
    }
    if (!g_rings_initialized) {
        if (!XHCIInitializeCommandAndEventRings(info)) {
            return false;
        }
    }

    if (g_command_enqueue_index >= 255) {
        g_command_enqueue_index = 0;
    }

    TRB& cmd = g_command_ring[g_command_enqueue_index];
    cmd.dword0 = 0;
    cmd.dword1 = 0;
    cmd.dword2 = 0;
    // Type=Enable Slot(9), Cycle bit=current producer cycle.
    cmd.dword3 = (9u << 10) | (g_command_cycle_bit ? 1u : 0u);
    ++g_command_enqueue_index;
    if (g_command_enqueue_index == 255) {
        g_command_enqueue_index = 0;
        g_command_cycle_bit ^= 1;
    }

    const uint64_t db = (reinterpret_cast<uint64_t>(info.operational_base) - info.cap_length) + (info.db_off & ~0x3u);
    WriteMMIO32(db + 0x00, 0);  // Ring command doorbell

    // Poll event ring for Command Completion Event.
    for (uint32_t i = 0; i < timeout_iters; ++i) {
        TRB& ev = g_event_ring[g_event_dequeue_index];
        const uint8_t cycle = static_cast<uint8_t>(ev.dword3 & 1u);
        if (cycle != g_event_cycle_bit) {
            continue;
        }

        const uint8_t type = static_cast<uint8_t>((ev.dword3 >> 10) & 0x3Fu);
        const uint8_t ccode = static_cast<uint8_t>((ev.dword2 >> 24) & 0xFFu);
        const uint8_t slot_id = static_cast<uint8_t>((ev.dword3 >> 24) & 0xFFu);

        out_result->trb_type = type;
        out_result->completion_code = ccode;
        out_result->slot_id = slot_id;
        out_result->ok = (type == 33 && ccode == 1);

        // Advance consumer pointer.
        ++g_event_dequeue_index;
        if (g_event_dequeue_index >= 256) {
            g_event_dequeue_index = 0;
            g_event_cycle_bit ^= 1;
        }

        const uint64_t runtime = (reinterpret_cast<uint64_t>(info.operational_base) - info.cap_length) + (info.rts_off & ~0x1Fu);
        const uint64_t intr0 = runtime + 0x20;
        const uint64_t erdp_addr = intr0 + 0x18;
        const uint64_t new_erdp = reinterpret_cast<uint64_t>(&g_event_ring[g_event_dequeue_index]) | (1u << 3);
        WriteMMIO64(erdp_addr, new_erdp);
        return true;
    }

    return false;
}
