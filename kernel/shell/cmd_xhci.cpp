#include <stdint.h>
#include "console.hpp"
#include "graphics/mouse.hpp"
#include "usb/xhci.hpp"
#include "timer.hpp"
#include "shell/text.hpp"
#include "shell/cmd_xhci.hpp"

extern Console* console;
extern MouseCursor* mouse_cursor;
extern XHCICapabilityInfo g_xhci_caps;
extern uint8_t g_last_xhci_slot_id;
extern bool g_xhci_hid_auto_enabled;
extern uint8_t g_xhci_hid_auto_slot;
extern uint32_t g_xhci_hid_auto_len;
extern uint8_t g_xhci_hid_auto_config_value;
extern uint8_t g_xhci_hid_auto_endpoint_address;
extern uint8_t g_xhci_hid_auto_endpoint_id;
extern uint64_t g_xhci_hid_last_poll_tick;
extern uint32_t g_xhci_hid_last_poll_reason;
extern uint8_t g_xhci_hid_last_poll_ccode;
extern uint32_t g_xhci_hid_last_poll_length;
extern uint32_t g_xhci_hid_auto_consecutive_failures;
extern uint64_t g_xhci_hid_auto_fail_count;
extern uint64_t g_xhci_hid_auto_recover_count;
extern uint64_t g_xhci_hid_next_recover_tick;
extern uint32_t g_xhci_hid_auto_consecutive_no_data;
extern uint32_t g_xhci_hid_auto_last_recover_reason;
extern uint64_t g_xhci_hid_auto_last_recover_tick;
extern uint64_t g_last_absolute_mouse_tick;
extern uint8_t g_hid_format_mode;
extern uint32_t g_hid_observed_max_raw;
extern uint32_t g_hid_sample_count;
extern bool g_hid_calibrated;
extern uint16_t g_hid_min_x;
extern uint16_t g_hid_min_y;
extern uint16_t g_hid_max_x;
extern uint16_t g_hid_max_y;
extern uint16_t g_hid_last_raw_x;
extern uint16_t g_hid_last_raw_y;
extern uint16_t g_hid_last_clamped_x;
extern uint16_t g_hid_last_clamped_y;
extern int g_hid_last_mapped_x;
extern int g_hid_last_mapped_y;
extern uint8_t g_hid_buttons_mask;
extern uint8_t g_mouse_buttons_current;

int ParseInt(const char* s);
void ResetHIDDecodeLearning();
bool PollHIDAndApply(uint8_t slot, uint32_t req_len, bool verbose, uint32_t timeout_iters = 3000000);
bool StartXHCIAutoMouse(uint32_t req_len, uint16_t mps, uint8_t interval);
void EnqueueAbsolutePointerEvent(int x, int y, int wheel, uint8_t buttons = 0);

namespace {
const char* XhciHidPollReasonName(uint32_t reason) {
    switch (reason) {
        case 0:
            return "none";
        case 1:
            return "no_data";
        case 2:
            return "transfer";
        case 3:
            return "decode";
        default:
            return "unknown";
    }
}
}  // namespace

bool ExecuteXHCICommand(const char* cmd, const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    if (StrEqual(cmd, "xhciinfo")) {
        const auto& info = GetXHCIControllerInfo();
        if (!info.found) {
            console->PrintLine("xhci: not found");
            return true;
        }
        console->Print("xhci: ");
        console->PrintDec(info.address.bus);
        console->Print(":");
        console->PrintDec(info.address.device);
        console->Print(".");
        console->PrintDec(info.address.function);
        console->Print(" vendor=0x");
        console->PrintHex(info.vendor_id, 4);
        console->Print(" device=0x");
        console->PrintHex(info.device_id, 4);
        console->Print(" mmio=0x");
        console->PrintHex(info.mmio_base, 16);
        console->Print("\n");
        if (g_xhci_caps.valid) {
            console->Print("xhci caplen=0x");
            console->PrintHex(g_xhci_caps.cap_length, 2);
            console->Print(" ver=0x");
            console->PrintHex(g_xhci_caps.hci_version, 4);
            console->Print(" slots=");
            console->PrintDec(g_xhci_caps.max_slots);
            console->Print(" intr=");
            console->PrintDec(g_xhci_caps.max_interrupters);
            console->Print(" ports=");
            console->PrintDec(g_xhci_caps.max_ports);
            console->Print(" pages=0x");
            console->PrintHex(g_xhci_caps.page_size_bitmap, 4);
            console->Print(" hcs1=0x");
            console->PrintHex(g_xhci_caps.hcs_params1, 8);
            console->Print(" hcc1=0x");
            console->PrintHex(g_xhci_caps.hcc_params1, 8);
            console->Print("\n");
        }
        return true;
    }

    if (StrEqual(cmd, "xhciregs")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciregs: xhci not ready");
            return true;
        }
        XHCIOperationalStatus st{};
        if (!ReadXHCIOperationalStatus(g_xhci_caps, &st) || !st.valid) {
            console->PrintLine("xhciregs: read failed");
            return true;
        }
        console->Print("usbcmd=0x");
        console->PrintHex(st.usbcmd, 8);
        console->Print(" usbsts=0x");
        console->PrintHex(st.usbsts, 8);
        console->Print(" config=0x");
        console->PrintHex(st.config, 8);
        console->Print("\n");
        console->Print("crcr=0x");
        console->PrintHex(st.crcr, 16);
        console->Print(" dcbaap=0x");
        console->PrintHex(st.dcbaap, 16);
        console->Print("\n");
        console->Print("flags: run=");
        console->Print(st.run_stop ? "1" : "0");
        console->Print(" halted=");
        console->Print(st.hc_halted ? "1" : "0");
        console->Print(" hse=");
        console->Print(st.host_system_error ? "1" : "0");
        console->Print(" eint=");
        console->Print(st.event_interrupt ? "1" : "0");
        console->Print(" pcd=");
        console->Print(st.port_change_detect ? "1" : "0");
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhcistop")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcistop: xhci not ready");
            return true;
        }
        if (XHCISetRunStop(g_xhci_caps, false)) {
            console->PrintLine("xhcistop: ok");
        } else {
            console->PrintLine("xhcistop: timeout");
        }
        return true;
    }

    if (StrEqual(cmd, "xhcistart")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcistart: xhci not ready");
            return true;
        }
        if (XHCISetRunStop(g_xhci_caps, true)) {
            console->PrintLine("xhcistart: ok");
        } else {
            console->PrintLine("xhcistart: timeout");
        }
        return true;
    }

    if (StrEqual(cmd, "xhcireset")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcireset: xhci not ready");
            return true;
        }
        if (XHCIResetController(g_xhci_caps)) {
            console->PrintLine("xhcireset: ok");
        } else {
            console->PrintLine("xhcireset: timeout");
        }
        return true;
    }

    if (StrEqual(cmd, "xhciinit")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciinit: xhci not ready");
            return true;
        }
        if (XHCIInitializeCommandAndEventRings(g_xhci_caps)) {
            console->PrintLine("xhciinit: ok");
        } else {
            console->PrintLine("xhciinit: failed");
        }
        return true;
    }

    if (StrEqual(cmd, "xhcienableslot")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcienableslot: xhci not ready");
            return true;
        }
        XHCICommandResult r{};
        if (!XHCIEnableSlot(g_xhci_caps, &r)) {
            console->PrintLine("xhcienableslot: timeout/fail");
            return true;
        }
        console->Print("xhcienableslot: type=");
        console->PrintDec(r.trb_type);
        console->Print(" ccode=");
        console->PrintDec(r.completion_code);
        console->Print(" slot=");
        console->PrintDec(r.slot_id);
        console->Print("\n");
        if (r.ok && r.slot_id > 0) {
            g_last_xhci_slot_id = r.slot_id;
        }
        return true;
    }

    if (StrEqual(cmd, "xhciaddress")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciaddress: xhci not ready");
            return true;
        }

        int slot = g_last_xhci_slot_id;
        int port = 0;
        int speed = 0;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            port = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            speed = ParseInt(t2);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhciaddress: invalid slot");
            return true;
        }
        if (port <= 0 || speed <= 0) {
            XHCIPortStatus ports[32];
            int n = ReadXHCIPortStatus(g_xhci_caps, ports, 32);
            for (int i = 0; i < n; ++i) {
                if (ports[i].connected) {
                    if (port <= 0) {
                        port = static_cast<int>(ports[i].port_id);
                    }
                    if (speed <= 0) {
                        speed = ports[i].speed;
                    }
                    break;
                }
            }
        }
        if (port <= 0 || speed <= 0) {
            console->PrintLine("xhciaddress: port/speed unknown");
            return true;
        }

        XHCIAddressDeviceResult ar{};
        if (!XHCIAddressDevice(g_xhci_caps,
                               static_cast<uint8_t>(slot),
                               static_cast<uint8_t>(port),
                               static_cast<uint8_t>(speed),
                               &ar)) {
            console->PrintLine("xhciaddress: timeout/fail");
            return true;
        }
        console->Print("xhciaddress: ccode=");
        console->PrintDec(ar.completion_code);
        console->Print(" slot=");
        console->PrintDec(ar.slot_id);
        console->Print(" port=");
        console->PrintDec(port);
        console->Print(" speed=");
        console->PrintDec(speed);
        console->Print("\n");
        if (ar.ok && ar.slot_id > 0) {
            g_last_xhci_slot_id = ar.slot_id;
        }
        return true;
    }

    if (StrEqual(cmd, "xhciconfigep")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciconfigep: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int mps = 8;
        int interval = 4;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            mps = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            interval = ParseInt(t2);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhciconfigep: invalid slot");
            return true;
        }
        if (mps <= 0 || mps > 1024) {
            console->PrintLine("xhciconfigep: invalid mps");
            return true;
        }
        if (interval <= 0 || interval > 255) {
            console->PrintLine("xhciconfigep: invalid interval");
            return true;
        }
        XHCIConfigureEndpointResult cr{};
        if (!XHCIConfigureInterruptInEndpoint(g_xhci_caps,
                                              static_cast<uint8_t>(slot),
                                              static_cast<uint16_t>(mps),
                                              static_cast<uint8_t>(interval),
                                              &cr)) {
            console->PrintLine("xhciconfigep: timeout/fail");
            return true;
        }
        console->Print("xhciconfigep: ccode=");
        console->PrintDec(cr.completion_code);
        console->Print(" slot=");
        console->PrintDec(cr.slot_id);
        console->Print(" mps=");
        console->PrintDec(mps);
        console->Print(" interval=");
        console->PrintDec(interval);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhciintrin")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciintrin: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int req_len = 8;
        int timeout = 3000000;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            timeout = ParseInt(t2);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhciintrin: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhciintrin: len must be 1..64");
            return true;
        }
        if (timeout <= 0) {
            console->PrintLine("xhciintrin: timeout must be > 0");
            return true;
        }

        XHCIInterruptInResult rr{};
        if (!XHCIPollInterruptIn(g_xhci_caps,
                                 static_cast<uint8_t>(slot),
                                 static_cast<uint32_t>(req_len),
                                 &rr,
                                 static_cast<uint32_t>(timeout))) {
            console->PrintLine("xhciintrin: timeout/fail");
            return true;
        }
        console->Print("xhciintrin: ccode=");
        console->PrintDec(rr.completion_code);
        console->Print(" slot=");
        console->PrintDec(rr.slot_id);
        console->Print(" ep=");
        console->PrintDec(rr.endpoint_id);
        console->Print(" len=");
        console->PrintDec(rr.data_length);
        console->Print(" data=");
        for (uint32_t i = 0; i < rr.data_length; ++i) {
            console->PrintHex(rr.data[i], 2);
            if (i + 1 < rr.data_length) {
                console->Print(" ");
            }
        }
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhcidesc")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcidesc: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int max_len = 64;
        char t0[16];
        char t1[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            max_len = ParseInt(t1);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhcidesc: invalid slot");
            return true;
        }
        if (max_len <= 0 || max_len > 128) {
            console->PrintLine("xhcidesc: len must be 1..128");
            return true;
        }
        uint8_t buf[128];
        uint16_t actual = 0;
        if (!XHCIReadConfigurationDescriptor(g_xhci_caps,
                                             static_cast<uint8_t>(slot),
                                             buf,
                                             static_cast<uint16_t>(max_len),
                                             &actual)) {
            console->PrintLine("xhcidesc: fail");
            return true;
        }
        console->Print("xhcidesc: slot=");
        console->PrintDec(slot);
        console->Print(" len=");
        console->PrintDec(actual);
        console->Print(" data=");
        for (uint16_t i = 0; i < actual; ++i) {
            console->PrintHex(buf[i], 2);
            if (i + 1 < actual) {
                console->Print(" ");
            }
        }
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhcihidpoll")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcihidpoll: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int req_len = 8;
        int timeout = 3000000;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            timeout = ParseInt(t2);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhcihidpoll: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhcihidpoll: len must be 1..64");
            return true;
        }
        if (timeout <= 0) {
            console->PrintLine("xhcihidpoll: timeout must be > 0");
            return true;
        }

        PollHIDAndApply(static_cast<uint8_t>(slot),
                        static_cast<uint32_t>(req_len),
                        true,
                        static_cast<uint32_t>(timeout));
        return true;
    }

    if (StrEqual(cmd, "xhcihidwatch")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcihidwatch: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int req_len = 8;
        int ticks = 180;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            ticks = ParseInt(t2);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhcihidwatch: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhcihidwatch: len must be 1..64");
            return true;
        }
        if (ticks <= 0 || ticks > 1800) {
            console->PrintLine("xhcihidwatch: ticks must be 1..1800");
            return true;
        }
        console->Print("xhcihidwatch: slot=");
        console->PrintDec(slot);
        console->Print(" len=");
        console->PrintDec(req_len);
        console->Print(" ticks=");
        console->PrintDec(ticks);
        console->Print("\n");
        const uint64_t start_tick = CurrentTick();
        const bool passive_auto_watch =
            g_xhci_hid_auto_enabled &&
            g_xhci_hid_auto_slot == static_cast<uint8_t>(slot) &&
            g_xhci_hid_auto_len == static_cast<uint32_t>(req_len);
        const uint64_t start_abs_tick = g_last_absolute_mouse_tick;
        const uint32_t start_samples = g_hid_sample_count;
        uint64_t polls = 0;
        while ((CurrentTick() - start_tick) < static_cast<uint64_t>(ticks)) {
            if (passive_auto_watch) {
                if (g_last_absolute_mouse_tick != start_abs_tick || g_hid_sample_count != start_samples) {
                    console->Print("xhcihidwatch: success polls=");
                    console->PrintDec(static_cast<int64_t>(polls));
                    console->Print(" elapsed_ticks=");
                    console->PrintDec(static_cast<int64_t>(CurrentTick() - start_tick));
                    console->Print(" mode=passive");
                    console->Print("\n");
                    return true;
                }
                continue;
            }
            ++polls;
            if (PollHIDAndApply(static_cast<uint8_t>(slot),
                                static_cast<uint32_t>(req_len),
                                true,
                                3000000u)) {
                console->Print("xhcihidwatch: success polls=");
                console->PrintDec(static_cast<int64_t>(polls));
                console->Print(" elapsed_ticks=");
                console->PrintDec(static_cast<int64_t>(CurrentTick() - start_tick));
                console->Print("\n");
                return true;
            }
        }
        console->Print("xhcihidwatch: no data polls=");
        console->PrintDec(static_cast<int64_t>(polls));
        console->Print(" elapsed_ticks=");
        console->PrintDec(static_cast<int64_t>(CurrentTick() - start_tick));
        if (passive_auto_watch) {
            console->Print(" mode=passive");
        }
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhcihidstat")) {
        char arg[16];
        if (NextToken(command, &pos, arg, sizeof(arg)) && StrEqual(arg, "reset")) {
            ResetHIDDecodeLearning();
            console->PrintLine("xhcihidstat: reset");
            return true;
        }
        console->Print("xhcihidstat: mode=");
        if (g_hid_format_mode == 2) {
            console->Print("B");
        } else if (g_hid_format_mode == 1) {
            console->Print("A");
        } else {
            console->Print("unknown");
        }
        console->Print(" max_raw=");
        console->PrintDec(g_hid_observed_max_raw);
        console->Print(" samples=");
        console->PrintDec(g_hid_sample_count);
        console->Print(" calib=");
        console->Print(g_hid_calibrated ? "1" : "0");
        console->Print(" min=(");
        console->PrintDec(g_hid_min_x);
        console->Print(",");
        console->PrintDec(g_hid_min_y);
        console->Print(") max=(");
        console->PrintDec(g_hid_max_x);
        console->Print(",");
        console->PrintDec(g_hid_max_y);
        console->Print(")");
        console->Print(" raw=(");
        console->PrintDec(g_hid_last_raw_x);
        console->Print(",");
        console->PrintDec(g_hid_last_raw_y);
        console->Print(")");
        console->Print(" clamp=(");
        console->PrintDec(g_hid_last_clamped_x);
        console->Print(",");
        console->PrintDec(g_hid_last_clamped_y);
        console->Print(")");
        console->Print(" map=(");
        console->PrintDec(g_hid_last_mapped_x);
        console->Print(",");
        console->PrintDec(g_hid_last_mapped_y);
        console->Print(")");
        console->Print(" btn=0x");
        console->PrintHex(g_hid_buttons_mask, 2);
        console->Print(" cfg=");
        console->PrintDec(g_xhci_hid_auto_config_value);
        console->Print(" ep=0x");
        console->PrintHex(g_xhci_hid_auto_endpoint_address, 2);
        console->Print(" dci=");
        console->PrintDec(g_xhci_hid_auto_endpoint_id);
        console->Print(" poll=");
        console->Print(XhciHidPollReasonName(g_xhci_hid_last_poll_reason));
        console->Print(" ccode=");
        console->PrintDec(g_xhci_hid_last_poll_ccode);
        console->Print(" len=");
        console->PrintDec(g_xhci_hid_last_poll_length);
        console->Print(" auto_fail=");
        console->PrintDec(g_xhci_hid_auto_fail_count);
        console->Print(" auto_nodata=");
        console->PrintDec(g_xhci_hid_auto_consecutive_no_data);
        console->Print(" auto_recover=");
        console->PrintDec(g_xhci_hid_auto_recover_count);
        console->Print(" rec.reason=");
        console->Print(XhciHidPollReasonName(g_xhci_hid_auto_last_recover_reason));
        console->Print(" rec.tick=");
        console->PrintDec(g_xhci_hid_auto_last_recover_tick);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhciauto")) {
        char mode[16];
        if (!NextToken(command, &pos, mode, sizeof(mode))) {
            console->Print("xhciauto: ");
            console->Print(g_xhci_hid_auto_enabled ? "on" : "off");
            console->Print(" slot=");
            console->PrintDec(g_xhci_hid_auto_slot);
            console->Print(" cfg=");
            console->PrintDec(g_xhci_hid_auto_config_value);
            console->Print(" ep=0x");
            console->PrintHex(g_xhci_hid_auto_endpoint_address, 2);
            console->Print(" dci=");
            console->PrintDec(g_xhci_hid_auto_endpoint_id);
            console->Print(" len=");
            console->PrintDec(g_xhci_hid_auto_len);
            console->Print("\n");
            return true;
        }
        if (StrEqual(mode, "off")) {
            g_xhci_hid_auto_enabled = false;
            g_xhci_hid_auto_consecutive_failures = 0;
            console->PrintLine("xhciauto: off");
            return true;
        }
        if (!StrEqual(mode, "on")) {
            console->PrintLine("xhciauto: use on/off");
            return true;
        }
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciauto: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int req_len = static_cast<int>(g_xhci_hid_auto_len);
        char t0[16];
        char t1[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhciauto: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhciauto: len must be 1..64");
            return true;
        }
        g_xhci_hid_auto_slot = static_cast<uint8_t>(slot);
        g_xhci_hid_auto_len = static_cast<uint32_t>(req_len);
        g_xhci_hid_auto_enabled = true;
        g_xhci_hid_last_poll_tick = CurrentTick();
        g_xhci_hid_auto_consecutive_failures = 0;
        g_xhci_hid_auto_fail_count = 0;
        g_xhci_hid_auto_recover_count = 0;
        g_xhci_hid_next_recover_tick = 0;
        g_xhci_hid_auto_last_recover_reason = 0;
        g_xhci_hid_auto_last_recover_tick = 0;
        ResetHIDDecodeLearning();
        console->Print("xhciauto: on slot=");
        console->PrintDec(slot);
        console->Print(" len=");
        console->PrintDec(req_len);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "xhciautostart")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhciautostart: xhci not ready");
            return true;
        }
        int req_len = 8;
        int mps = 8;
        int interval = 4;
        char t0[16];
        char t1[16];
        char t2[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            req_len = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            mps = ParseInt(t1);
        }
        if (NextToken(command, &pos, t2, sizeof(t2))) {
            interval = ParseInt(t2);
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhciautostart: len must be 1..64");
            return true;
        }
        if (mps <= 0 || mps > 1024) {
            console->PrintLine("xhciautostart: mps must be 1..1024");
            return true;
        }
        if (interval <= 0 || interval > 255) {
            console->PrintLine("xhciautostart: interval must be 1..255");
            return true;
        }
        ResetHIDDecodeLearning();
        g_xhci_hid_auto_fail_count = 0;
        g_xhci_hid_auto_recover_count = 0;
        g_xhci_hid_auto_consecutive_failures = 0;
        g_xhci_hid_auto_consecutive_no_data = 0;
        g_xhci_hid_next_recover_tick = 0;
        g_xhci_hid_auto_last_recover_reason = 0;
        g_xhci_hid_auto_last_recover_tick = 0;
        if (!StartXHCIAutoMouse(static_cast<uint32_t>(req_len),
                                static_cast<uint16_t>(mps),
                                static_cast<uint8_t>(interval))) {
            console->PrintLine("xhciautostart: failed");
            return true;
        }
        console->Print("xhciautostart: ok slot=");
        console->PrintDec(g_xhci_hid_auto_slot);
        console->Print(" len=");
        console->PrintDec(g_xhci_hid_auto_len);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "mouseabs")) {
        char sx[16];
        char sy[16];
        if (!NextToken(command, &pos, sx, sizeof(sx)) ||
            !NextToken(command, &pos, sy, sizeof(sy))) {
            console->PrintLine("mouseabs: x y required");
            return true;
        }
        int x = ParseInt(sx);
        int y = ParseInt(sy);
        EnqueueAbsolutePointerEvent(x, y, 0);
        return true;
    }

    if (StrEqual(cmd, "mousepos")) {
        if (mouse_cursor == nullptr) {
            console->PrintLine("mousepos: cursor unavailable");
            return true;
        }
        console->Print("mousepos: x=");
        console->PrintDec(mouse_cursor->X());
        console->Print(" y=");
        console->PrintDec(mouse_cursor->Y());
        console->Print(" btn=0x");
        console->PrintHex(g_mouse_buttons_current, 2);
        console->Print(" abs_tick=");
        console->PrintDec(static_cast<int64_t>(g_last_absolute_mouse_tick));
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "mousebtn")) {
        if (mouse_cursor == nullptr) {
            console->PrintLine("mousebtn: cursor unavailable");
            return true;
        }
        char smask[16];
        if (!NextToken(command, &pos, smask, sizeof(smask))) {
            console->Print("mousebtn: btn=0x");
            console->PrintHex(g_mouse_buttons_current, 2);
            console->Print("\n");
            return true;
        }
        int mask = ParseInt(smask);
        if (mask < 0 || mask > 255) {
            console->PrintLine("mousebtn: mask must be 0..255");
            return true;
        }
        EnqueueAbsolutePointerEvent(mouse_cursor->X(), mouse_cursor->Y(), 0,
                                    static_cast<uint8_t>(mask));
        console->Print("mousebtn: btn=0x");
        console->PrintHex(static_cast<uint8_t>(mask), 2);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "mouseclick")) {
        if (mouse_cursor == nullptr) {
            console->PrintLine("mouseclick: cursor unavailable");
            return true;
        }
        char which[16];
        if (!NextToken(command, &pos, which, sizeof(which))) {
            console->PrintLine("mouseclick: use left/right/middle");
            return true;
        }
        uint8_t mask = 0;
        if (StrEqual(which, "left")) {
            mask = 1;
        } else if (StrEqual(which, "right")) {
            mask = 2;
        } else if (StrEqual(which, "middle")) {
            mask = 4;
        } else {
            console->PrintLine("mouseclick: use left/right/middle");
            return true;
        }
        EnqueueAbsolutePointerEvent(mouse_cursor->X(), mouse_cursor->Y(), 0, mask);
        EnqueueAbsolutePointerEvent(mouse_cursor->X(), mouse_cursor->Y(), 0, 0);
        console->Print("mouseclick: ");
        console->Print(which);
        console->Print("\n");
        return true;
    }

    if (StrEqual(cmd, "usbports")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("usbports: xhci not ready");
            return true;
        }
        XHCIPortStatus ports[32];
        int n = ReadXHCIPortStatus(g_xhci_caps, ports, 32);
        if (n <= 0) {
            console->PrintLine("usbports: no ports");
            return true;
        }
        for (int i = 0; i < n; ++i) {
            console->Print("port ");
            console->PrintDec(ports[i].port_id);
            console->Print(": conn=");
            console->Print(ports[i].connected ? "1" : "0");
            console->Print(" en=");
            console->Print(ports[i].enabled ? "1" : "0");
            console->Print(" pwr=");
            console->Print(ports[i].power ? "1" : "0");
            console->Print(" spd=");
            console->PrintDec(ports[i].speed);
            console->Print(" raw=0x");
            console->PrintHex(ports[i].raw_portsc, 8);
            console->Print("\n");
        }
        return true;
    }

    return false;
}

