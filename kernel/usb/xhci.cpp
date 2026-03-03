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
    return true;
}
