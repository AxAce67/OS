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

struct XHCISlotState {
    bool used;
    bool addressed;
    bool interrupt_in_configured;
    uint8_t root_port;
    uint8_t port_speed;
    uint8_t interrupt_in_endpoint_id;
    uint16_t interrupt_in_max_packet_size;
    uint8_t interrupt_in_interval;
};

alignas(64) uint64_t g_dcbaa[256];
alignas(64) TRB g_command_ring[256];
alignas(64) TRB g_event_ring[256];
alignas(64) EventRingSegmentTableEntry g_erst[1];
const int kXHCIMaxManagedSlots = 16;
alignas(64) uint8_t g_input_contexts[kXHCIMaxManagedSlots][1024];
alignas(64) uint8_t g_device_contexts[kXHCIMaxManagedSlots][1024];
alignas(64) TRB g_ep0_rings[kXHCIMaxManagedSlots][256];
alignas(64) TRB g_ep1in_rings[kXHCIMaxManagedSlots][256];
XHCISlotState g_slot_states[kXHCIMaxManagedSlots];
uint8_t g_ep0_cycle_bits[kXHCIMaxManagedSlots];
uint16_t g_ep0_enqueue_indices[kXHCIMaxManagedSlots];
uint8_t g_ep1_cycle_bits[kXHCIMaxManagedSlots];
uint16_t g_ep1_enqueue_indices[kXHCIMaxManagedSlots];
alignas(64) uint8_t g_ep1_buffers[kXHCIMaxManagedSlots][64];
alignas(64) uint8_t g_ep0_control_buffers[kXHCIMaxManagedSlots][256];
bool g_rings_initialized = false;
uint32_t g_command_enqueue_index = 0;
uint8_t g_command_cycle_bit = 1;
uint32_t g_event_dequeue_index = 0;
uint8_t g_event_cycle_bit = 1;

int SlotIndexFromId(uint8_t slot_id);

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

uint64_t ControllerBase(const XHCICapabilityInfo& info) {
    return info.operational_base - info.cap_length;
}

uint64_t RuntimeBase(const XHCICapabilityInfo& info) {
    return ControllerBase(info) + (info.rts_off & ~0x1Fu);
}

uint64_t DoorbellBase(const XHCICapabilityInfo& info) {
    return ControllerBase(info) + (info.db_off & ~0x3u);
}

void RingCommandDoorbell(const XHCICapabilityInfo& info) {
    WriteMMIO32(DoorbellBase(info) + 0x00, 0);
}

void RingEndpointDoorbell(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t endpoint_id) {
    if (slot_id == 0) {
        return;
    }
    const uint64_t db = DoorbellBase(info) + static_cast<uint64_t>(slot_id) * 4;
    WriteMMIO32(db, static_cast<uint32_t>(endpoint_id));
}

void AdvanceEventDequeue(const XHCICapabilityInfo& info) {
    ++g_event_dequeue_index;
    if (g_event_dequeue_index >= 256) {
        g_event_dequeue_index = 0;
        g_event_cycle_bit ^= 1;
    }
    const uint64_t intr0 = RuntimeBase(info) + 0x20;
    const uint64_t erdp_addr = intr0 + 0x18;
    const uint64_t new_erdp = reinterpret_cast<uint64_t>(&g_event_ring[g_event_dequeue_index]) | (1u << 3);
    WriteMMIO64(erdp_addr, new_erdp);
}

bool SubmitCommandAndWait(const XHCICapabilityInfo& info, const TRB& command, XHCICommandResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = 0;
    out_result->trb_type = 0;

    if (g_command_enqueue_index >= 255) {
        g_command_enqueue_index = 0;
    }

    TRB cmd = command;
    cmd.dword3 = (cmd.dword3 & ~1u) | (g_command_cycle_bit ? 1u : 0u);
    g_command_ring[g_command_enqueue_index] = cmd;
    ++g_command_enqueue_index;
    if (g_command_enqueue_index == 255) {
        g_command_enqueue_index = 0;
        g_command_cycle_bit ^= 1;
    }

    RingCommandDoorbell(info);

    for (uint32_t i = 0; i < timeout_iters; ++i) {
        TRB& ev = g_event_ring[g_event_dequeue_index];
        const uint8_t cycle = static_cast<uint8_t>(ev.dword3 & 1u);
        if (cycle != g_event_cycle_bit) {
            continue;
        }

        const uint8_t type = static_cast<uint8_t>((ev.dword3 >> 10) & 0x3Fu);
        const uint8_t ccode = static_cast<uint8_t>((ev.dword2 >> 24) & 0xFFu);
        const uint8_t slot_id = static_cast<uint8_t>((ev.dword3 >> 24) & 0xFFu);
        AdvanceEventDequeue(info);

        if (type != 33) {
            continue;
        }
        out_result->trb_type = type;
        out_result->completion_code = ccode;
        out_result->slot_id = slot_id;
        out_result->ok = (ccode == 1);
        return true;
    }
    return false;
}

bool WaitTransferEvent(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t endpoint_id, XHCIInterruptInResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < timeout_iters; ++i) {
        TRB& ev = g_event_ring[g_event_dequeue_index];
        const uint8_t cycle = static_cast<uint8_t>(ev.dword3 & 1u);
        if (cycle != g_event_cycle_bit) {
            continue;
        }

        const uint8_t type = static_cast<uint8_t>((ev.dword3 >> 10) & 0x3Fu);
        const uint8_t ev_endpoint_id = static_cast<uint8_t>((ev.dword3 >> 16) & 0x1Fu);
        const uint8_t ev_slot_id = static_cast<uint8_t>((ev.dword3 >> 24) & 0xFFu);
        const uint8_t ccode = static_cast<uint8_t>((ev.dword2 >> 24) & 0xFFu);
        const uint32_t residual = (ev.dword2 & 0x00FFFFFFu);
        AdvanceEventDequeue(info);

        if (type != 32) {  // Transfer Event
            continue;
        }
        if (ev_slot_id != slot_id || ev_endpoint_id != endpoint_id) {
            continue;
        }

        out_result->ok = (ccode == 1);
        out_result->completion_code = ccode;
        out_result->slot_id = ev_slot_id;
        out_result->endpoint_id = ev_endpoint_id;
        out_result->transfer_length = residual;
        return true;
    }
    return false;
}

bool SubmitControlTransfer(const XHCICapabilityInfo& info,
                           uint8_t slot_id,
                           uint8_t bm_request_type,
                           uint8_t b_request,
                           uint16_t w_value,
                           uint16_t w_index,
                           uint16_t w_length,
                           bool data_in,
                           uint8_t* data_buffer,
                           XHCIControlTransferResult* out_result,
                           uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = slot_id;
    out_result->endpoint_id = 1;

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0 || !g_slot_states[idx].used || !g_slot_states[idx].addressed) {
        return false;
    }
    if (!g_rings_initialized) {
        return false;
    }

    uint16_t enqueue = g_ep0_enqueue_indices[idx];
    if (enqueue >= 255) {
        enqueue = 0;
    }

    TRB setup{};
    setup.dword0 = static_cast<uint32_t>(w_value << 16) |
                   static_cast<uint32_t>(b_request << 8) |
                   static_cast<uint32_t>(bm_request_type);
    setup.dword1 = static_cast<uint32_t>(w_length << 16) |
                   static_cast<uint32_t>(w_index);
    setup.dword2 = 8u;
    uint32_t trt = 0;
    if (w_length != 0) {
        trt = data_in ? 3u : 2u;
    }
    setup.dword3 = (2u << 10) | (trt << 16) | (1u << 6) | (g_ep0_cycle_bits[idx] ? 1u : 0u);
    g_ep0_rings[idx][enqueue] = setup;
    ++enqueue;
    if (enqueue == 255) {
        enqueue = 0;
        g_ep0_cycle_bits[idx] ^= 1;
    }

    if (w_length != 0) {
        TRB data{};
        const uint64_t buf_ptr = reinterpret_cast<uint64_t>(data_buffer);
        data.dword0 = static_cast<uint32_t>(buf_ptr & 0xFFFFFFFFu);
        data.dword1 = static_cast<uint32_t>(buf_ptr >> 32);
        data.dword2 = w_length;
        data.dword3 = (3u << 10) | (data_in ? (1u << 16) : 0u) | (g_ep0_cycle_bits[idx] ? 1u : 0u);
        g_ep0_rings[idx][enqueue] = data;
        ++enqueue;
        if (enqueue == 255) {
            enqueue = 0;
            g_ep0_cycle_bits[idx] ^= 1;
        }
    }

    TRB status{};
    status.dword0 = 0;
    status.dword1 = 0;
    status.dword2 = 0;
    const bool status_in = (w_length == 0);
    status.dword3 = (4u << 10) | (status_in ? (1u << 16) : 0u) | (1u << 5) |
                    (g_ep0_cycle_bits[idx] ? 1u : 0u);
    g_ep0_rings[idx][enqueue] = status;
    ++enqueue;
    if (enqueue == 255) {
        enqueue = 0;
        g_ep0_cycle_bits[idx] ^= 1;
    }
    g_ep0_enqueue_indices[idx] = enqueue;

    RingEndpointDoorbell(info, slot_id, 1);

    XHCIInterruptInResult ev{};
    if (!WaitTransferEvent(info, slot_id, 1, &ev, timeout_iters)) {
        return false;
    }
    out_result->ok = ev.ok;
    out_result->completion_code = ev.completion_code;
    out_result->slot_id = ev.slot_id;
    out_result->endpoint_id = ev.endpoint_id;
    return true;
}

uint8_t* ContextPtr(uint8_t* input_ctx, int index, int ctx_size) {
    return input_ctx + static_cast<uint32_t>(index * ctx_size);
}

uint16_t DefaultControlMaxPacketSize(uint8_t speed) {
    switch (speed) {
        case 3: return 64;   // High-speed
        case 4: return 512;  // SuperSpeed
        default: return 8;   // Low/Full or unknown
    }
}

int SlotIndexFromId(uint8_t slot_id) {
    if (slot_id == 0 || slot_id > kXHCIMaxManagedSlots) {
        return -1;
    }
    return static_cast<int>(slot_id - 1);
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
    MemorySet(g_input_contexts, 0, sizeof(g_input_contexts));
    MemorySet(g_device_contexts, 0, sizeof(g_device_contexts));
    MemorySet(g_ep0_rings, 0, sizeof(g_ep0_rings));
    MemorySet(g_ep1in_rings, 0, sizeof(g_ep1in_rings));
    MemorySet(g_slot_states, 0, sizeof(g_slot_states));
    MemorySet(g_ep0_cycle_bits, 0, sizeof(g_ep0_cycle_bits));
    MemorySet(g_ep0_enqueue_indices, 0, sizeof(g_ep0_enqueue_indices));
    MemorySet(g_ep1_cycle_bits, 0, sizeof(g_ep1_cycle_bits));
    MemorySet(g_ep1_enqueue_indices, 0, sizeof(g_ep1_enqueue_indices));
    MemorySet(g_ep1_buffers, 0, sizeof(g_ep1_buffers));

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
    const uint64_t runtime = RuntimeBase(info);
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

    for (int i = 0; i < kXHCIMaxManagedSlots; ++i) {
        TRB& ep_link = g_ep0_rings[i][255];
        const uint64_t ep_ring_base = reinterpret_cast<uint64_t>(&g_ep0_rings[i][0]);
        ep_link.dword0 = static_cast<uint32_t>(ep_ring_base & 0xFFFFFFFFu);
        ep_link.dword1 = static_cast<uint32_t>(ep_ring_base >> 32);
        ep_link.dword2 = 0;
        ep_link.dword3 = (6u << 10) | (1u << 1);
        g_ep0_cycle_bits[i] = 1;
        g_ep0_enqueue_indices[i] = 0;

        TRB& ep1_link = g_ep1in_rings[i][255];
        const uint64_t ep1_ring_base = reinterpret_cast<uint64_t>(&g_ep1in_rings[i][0]);
        ep1_link.dword0 = static_cast<uint32_t>(ep1_ring_base & 0xFFFFFFFFu);
        ep1_link.dword1 = static_cast<uint32_t>(ep1_ring_base >> 32);
        ep1_link.dword2 = 0;
        ep1_link.dword3 = (6u << 10) | (1u << 1);
        g_ep1_cycle_bits[i] = 1;
        g_ep1_enqueue_indices[i] = 0;
    }

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
    TRB cmd{};
    cmd.dword0 = 0;
    cmd.dword1 = 0;
    cmd.dword2 = 0;
    cmd.dword3 = (9u << 10);  // Enable Slot
    return SubmitCommandAndWait(info, cmd, out_result, timeout_iters);
}

bool XHCIAddressDevice(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t root_port, uint8_t port_speed, XHCIAddressDeviceResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = slot_id;

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0) {
        return false;
    }
    if (!g_rings_initialized) {
        if (!XHCIInitializeCommandAndEventRings(info)) {
            return false;
        }
    }

    const int ctx_size = ((info.hcc_params1 & (1u << 2)) != 0) ? 64 : 32;

    uint8_t* input_ctx = &g_input_contexts[idx][0];
    uint8_t* device_ctx = &g_device_contexts[idx][0];
    MemorySet(input_ctx, 0, 1024);
    MemorySet(device_ctx, 0, 1024);

    // DCBAA[slot_id] points to output device context.
    g_dcbaa[slot_id] = reinterpret_cast<uint64_t>(device_ctx);

    // Input Control Context (index 0): add slot and ep0 contexts.
    uint32_t* icc = reinterpret_cast<uint32_t*>(ContextPtr(input_ctx, 0, ctx_size));
    icc[0] = 0;                               // Drop Context Flags
    icc[1] = (1u << 0) | (1u << 1);          // Add Slot + EP0

    // Slot Context (index 1 in input context layout)
    uint32_t* slot_ctx = reinterpret_cast<uint32_t*>(ContextPtr(input_ctx, 1, ctx_size));
    slot_ctx[0] = (1u << 27) | (static_cast<uint32_t>(port_speed & 0x0F) << 20);  // ContextEntries=1, Speed
    slot_ctx[1] = (static_cast<uint32_t>(root_port) << 16);                        // Root Hub Port Number

    // EP0 Context (index 2 in input context layout)
    uint32_t* ep0_ctx = reinterpret_cast<uint32_t*>(ContextPtr(input_ctx, 2, ctx_size));
    const uint16_t mps = DefaultControlMaxPacketSize(port_speed);
    const uint64_t ep_ring = reinterpret_cast<uint64_t>(&g_ep0_rings[idx][0]) | 1u;  // DCS=1
    ep0_ctx[0] = 0;
    ep0_ctx[1] = (3u << 1) | (4u << 3) | (static_cast<uint32_t>(mps) << 16);  // CErr=3, EPType=Control
    ep0_ctx[2] = static_cast<uint32_t>(ep_ring & 0xFFFFFFFFu);
    ep0_ctx[3] = static_cast<uint32_t>(ep_ring >> 32);
    ep0_ctx[4] = 8;  // Avg TRB Length (small default)

    TRB cmd{};
    const uint64_t input_ctx_ptr = reinterpret_cast<uint64_t>(input_ctx);
    cmd.dword0 = static_cast<uint32_t>(input_ctx_ptr & 0xFFFFFFFFu);
    cmd.dword1 = static_cast<uint32_t>(input_ctx_ptr >> 32);
    cmd.dword2 = 0;
    // Type=Address Device(11), BSR=0, Slot ID in bits 31:24.
    cmd.dword3 = (11u << 10) | (static_cast<uint32_t>(slot_id) << 24);

    XHCICommandResult r{};
    if (!SubmitCommandAndWait(info, cmd, &r, timeout_iters)) {
        return false;
    }
    out_result->completion_code = r.completion_code;
    out_result->slot_id = r.slot_id;
    out_result->ok = r.ok;
    if (r.ok) {
        g_slot_states[idx].used = true;
        g_slot_states[idx].addressed = true;
        g_slot_states[idx].interrupt_in_configured = false;
        g_slot_states[idx].root_port = root_port;
        g_slot_states[idx].port_speed = port_speed;
        g_slot_states[idx].interrupt_in_endpoint_id = 3;
        g_slot_states[idx].interrupt_in_max_packet_size = 8;
        g_slot_states[idx].interrupt_in_interval = 4;
    }
    return true;
}

bool XHCIConfigureInterruptInEndpointEx(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t endpoint_id, uint16_t max_packet_size, uint8_t interval, XHCIConfigureEndpointResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = slot_id;

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0 || !g_slot_states[idx].used || !g_slot_states[idx].addressed) {
        return false;
    }
    if (endpoint_id < 2 || endpoint_id > 31) {
        return false;
    }
    if (!g_rings_initialized) {
        return false;
    }
    if (max_packet_size == 0) {
        max_packet_size = 8;
    }
    if (interval == 0) {
        interval = 4;
    }

    const int ctx_size = ((info.hcc_params1 & (1u << 2)) != 0) ? 64 : 32;
    uint8_t* input_ctx = &g_input_contexts[idx][0];
    uint8_t* device_ctx = &g_device_contexts[idx][0];
    MemorySet(input_ctx, 0, 1024);

    // Copy current slot and EP0 contexts from output device context to input context.
    uint8_t* in_slot = ContextPtr(input_ctx, 1, ctx_size);
    uint8_t* in_ep0 = ContextPtr(input_ctx, 2, ctx_size);
    uint8_t* out_slot = ContextPtr(device_ctx, 1, ctx_size);
    uint8_t* out_ep0 = ContextPtr(device_ctx, 2, ctx_size);
    for (int i = 0; i < ctx_size; ++i) {
        in_slot[i] = out_slot[i];
        in_ep0[i] = out_ep0[i];
    }

    uint32_t* icc = reinterpret_cast<uint32_t*>(ContextPtr(input_ctx, 0, ctx_size));
    icc[0] = 0;                               // Drop flags
    icc[1] = (1u << 0) | (1u << endpoint_id); // Add Slot + Interrupt IN endpoint

    // Update slot context entries to include the target DCI.
    uint32_t* slot_ctx = reinterpret_cast<uint32_t*>(in_slot);
    slot_ctx[0] &= ~(0x1Fu << 27);
    slot_ctx[0] |= (static_cast<uint32_t>(endpoint_id) << 27);

    const int ep_ctx_index = static_cast<int>(endpoint_id) + 1;
    uint32_t* ep1in_ctx = reinterpret_cast<uint32_t*>(ContextPtr(input_ctx, ep_ctx_index, ctx_size));
    const uint64_t ep1_ring = reinterpret_cast<uint64_t>(&g_ep1in_rings[idx][0]) | 1u;
    ep1in_ctx[0] = (static_cast<uint32_t>(interval) << 16); // Interval
    ep1in_ctx[1] = (3u << 1) | (7u << 3) | (static_cast<uint32_t>(max_packet_size) << 16); // CErr=3, Interrupt IN
    ep1in_ctx[2] = static_cast<uint32_t>(ep1_ring & 0xFFFFFFFFu);
    ep1in_ctx[3] = static_cast<uint32_t>(ep1_ring >> 32);
    // Average TRB Length and Max ESIT Payload are both required for Interrupt endpoints.
    ep1in_ctx[4] = static_cast<uint32_t>(max_packet_size) |
                   (static_cast<uint32_t>(max_packet_size) << 16);

    TRB cmd{};
    const uint64_t input_ctx_ptr = reinterpret_cast<uint64_t>(input_ctx);
    cmd.dword0 = static_cast<uint32_t>(input_ctx_ptr & 0xFFFFFFFFu);
    cmd.dword1 = static_cast<uint32_t>(input_ctx_ptr >> 32);
    cmd.dword2 = 0;
    cmd.dword3 = (12u << 10) | (static_cast<uint32_t>(slot_id) << 24); // Configure Endpoint

    XHCICommandResult r{};
    if (!SubmitCommandAndWait(info, cmd, &r, timeout_iters)) {
        return false;
    }
    out_result->completion_code = r.completion_code;
    out_result->slot_id = r.slot_id;
    out_result->ok = r.ok;
    if (r.ok) {
        g_slot_states[idx].interrupt_in_configured = true;
        g_slot_states[idx].interrupt_in_endpoint_id = endpoint_id;
        g_slot_states[idx].interrupt_in_max_packet_size = max_packet_size;
        g_slot_states[idx].interrupt_in_interval = interval;
    }
    return true;
}

bool XHCIConfigureInterruptInEndpoint(const XHCICapabilityInfo& info, uint8_t slot_id, uint16_t max_packet_size, uint8_t interval, XHCIConfigureEndpointResult* out_result, uint32_t timeout_iters) {
    return XHCIConfigureInterruptInEndpointEx(info, slot_id, 3, max_packet_size, interval, out_result, timeout_iters);
}

bool XHCISetConfiguration(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t configuration_value, XHCIControlTransferResult* out_result, uint32_t timeout_iters) {
    return SubmitControlTransfer(info, slot_id, 0x00, 9u, configuration_value, 0, 0, false, nullptr, out_result, timeout_iters);
}

bool XHCISetHidIdle(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t interface_number, uint8_t duration, uint8_t report_id, XHCIControlTransferResult* out_result, uint32_t timeout_iters) {
    const uint16_t w_value = static_cast<uint16_t>((static_cast<uint16_t>(duration) << 8) | report_id);
    return SubmitControlTransfer(info, slot_id, 0x21, 0x0Au, w_value, interface_number, 0, false, nullptr, out_result, timeout_iters);
}

bool XHCIReadConfigurationDescriptor(const XHCICapabilityInfo& info, uint8_t slot_id, uint8_t* out_buffer, uint16_t buffer_len, uint16_t* out_actual_length, uint32_t timeout_iters) {
    if (out_actual_length != nullptr) {
        *out_actual_length = 0;
    }
    if (out_buffer == nullptr || buffer_len == 0) {
        return false;
    }

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0 || !g_slot_states[idx].used || !g_slot_states[idx].addressed) {
        return false;
    }

    if (buffer_len > sizeof(g_ep0_control_buffers[idx])) {
        buffer_len = static_cast<uint16_t>(sizeof(g_ep0_control_buffers[idx]));
    }
    MemorySet(g_ep0_control_buffers[idx], 0, sizeof(g_ep0_control_buffers[idx]));

    XHCIControlTransferResult tr{};
    if (!SubmitControlTransfer(info, slot_id, 0x80, 6u, static_cast<uint16_t>(2u << 8), 0, buffer_len, true,
                               &g_ep0_control_buffers[idx][0], &tr, timeout_iters) ||
        !tr.ok) {
        return false;
    }

    uint16_t actual = buffer_len;
    if (buffer_len >= 4) {
        const uint16_t total_length = static_cast<uint16_t>(g_ep0_control_buffers[idx][2] |
                                                            (static_cast<uint16_t>(g_ep0_control_buffers[idx][3]) << 8));
        if (total_length != 0 && total_length < actual) {
            actual = total_length;
        }
    }
    for (uint16_t i = 0; i < actual; ++i) {
        out_buffer[i] = g_ep0_control_buffers[idx][i];
    }
    if (out_actual_length != nullptr) {
        *out_actual_length = actual;
    }
    return true;
}

bool XHCIFindFirstInterruptInEndpoint(const XHCICapabilityInfo& info, uint8_t slot_id, XHCIInterruptEndpointInfo* out_info, uint32_t timeout_iters) {
    if (out_info == nullptr) {
        return false;
    }
    out_info->ok = false;
    out_info->configuration_value = 1;
    out_info->interface_number = 0;
    out_info->endpoint_address = 0x81;
    out_info->endpoint_id = 3;
    out_info->max_packet_size = 8;
    out_info->interval = 4;

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0 || !g_slot_states[idx].used || !g_slot_states[idx].addressed) {
        return false;
    }

    uint16_t actual_length = 0;
    if (!XHCIReadConfigurationDescriptor(info, slot_id, &g_ep0_control_buffers[idx][0], 64, &actual_length, timeout_iters)) {
        return false;
    }

    const uint8_t* desc = &g_ep0_control_buffers[idx][0];
    if (desc[0] < 9 || desc[1] != 2) {
        return false;
    }
    out_info->configuration_value = desc[5];
    const uint16_t scan_len = actual_length;
    uint8_t current_interface_number = 0;
    uint16_t off = 0;
    while (off + 2 <= scan_len) {
        const uint8_t len = desc[off + 0];
        const uint8_t type = desc[off + 1];
        if (len < 2 || off + len > scan_len) {
            break;
        }
        if (type == 4 && len >= 9) {
            current_interface_number = desc[off + 2];
        }
        if (type == 5 && len >= 7) {
            const uint8_t ep_addr = desc[off + 2];
            const uint8_t attrs = desc[off + 3];
            if ((ep_addr & 0x80u) != 0 && (attrs & 0x03u) == 0x03u) {
                out_info->ok = true;
                out_info->endpoint_address = ep_addr;
                out_info->interface_number = current_interface_number;
                out_info->endpoint_id = static_cast<uint8_t>(((ep_addr & 0x0Fu) * 2u) + 1u);
                out_info->max_packet_size = static_cast<uint16_t>(desc[off + 4] | (static_cast<uint16_t>(desc[off + 5]) << 8));
                out_info->interval = desc[off + 6];
                return true;
            }
        }
        off = static_cast<uint16_t>(off + len);
    }
    return false;
}

bool XHCIPollInterruptIn(const XHCICapabilityInfo& info, uint8_t slot_id, uint32_t request_length, XHCIInterruptInResult* out_result, uint32_t timeout_iters) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->ok = false;
    out_result->completion_code = 0;
    out_result->slot_id = slot_id;
    out_result->endpoint_id = 3;
    out_result->transfer_length = 0;
    out_result->data_length = 0;
    MemorySet(out_result->data, 0, sizeof(out_result->data));

    const int idx = SlotIndexFromId(slot_id);
    if (!info.valid || idx < 0 || !g_slot_states[idx].interrupt_in_configured) {
        return false;
    }
    if (request_length == 0) {
        request_length = g_slot_states[idx].interrupt_in_max_packet_size;
    }
    if (request_length > sizeof(g_ep1_buffers[idx])) {
        request_length = sizeof(g_ep1_buffers[idx]);
    }

    uint16_t enqueue = g_ep1_enqueue_indices[idx];
    if (enqueue >= 255) {
        enqueue = 0;
    }

    const uint64_t buf_ptr = reinterpret_cast<uint64_t>(&g_ep1_buffers[idx][0]);
    TRB trb{};
    trb.dword0 = static_cast<uint32_t>(buf_ptr & 0xFFFFFFFFu);
    trb.dword1 = static_cast<uint32_t>(buf_ptr >> 32);
    trb.dword2 = request_length;  // transfer length
    // Normal TRB(Type=1), ISP=1, IOC=1, cycle=producer cycle.
    trb.dword3 = (1u << 10) | (1u << 5) | (1u << 2) | (g_ep1_cycle_bits[idx] ? 1u : 0u);
    g_ep1in_rings[idx][enqueue] = trb;

    ++enqueue;
    if (enqueue == 255) {
        enqueue = 0;
        g_ep1_cycle_bits[idx] ^= 1;
    }
    g_ep1_enqueue_indices[idx] = enqueue;

    const uint8_t endpoint_id = g_slot_states[idx].interrupt_in_endpoint_id;
    RingEndpointDoorbell(info, slot_id, endpoint_id);

    if (!WaitTransferEvent(info, slot_id, endpoint_id, out_result, timeout_iters)) {
        return false;
    }

    if (out_result->ok) {
        uint32_t residual = out_result->transfer_length;
        uint32_t actual = (request_length > residual) ? (request_length - residual) : 0;
        if (actual > sizeof(out_result->data)) {
            actual = sizeof(out_result->data);
        }
        for (uint32_t i = 0; i < actual; ++i) {
            out_result->data[i] = g_ep1_buffers[idx][i];
        }
        out_result->data_length = actual;
    }
    return true;
}
