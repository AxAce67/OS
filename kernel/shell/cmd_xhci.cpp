#include <stdint.h>
#include "console.hpp"
#include "usb/xhci.hpp"
#include "timer.hpp"
#include "shell/text.hpp"
#include "shell/cmd_xhci.hpp"

extern Console* console;
extern XHCICapabilityInfo g_xhci_caps;
extern uint8_t g_last_xhci_slot_id;
extern bool g_xhci_hid_auto_enabled;
extern uint8_t g_xhci_hid_auto_slot;
extern uint32_t g_xhci_hid_auto_len;
extern uint64_t g_xhci_hid_last_poll_tick;
extern uint32_t g_xhci_hid_auto_consecutive_failures;
extern uint64_t g_xhci_hid_auto_fail_count;
extern uint64_t g_xhci_hid_auto_recover_count;
extern uint64_t g_xhci_hid_next_recover_tick;
extern uint8_t g_hid_format_mode;
extern uint32_t g_hid_observed_max_raw;
extern uint32_t g_hid_sample_count;
extern bool g_hid_calibrated;
extern uint16_t g_hid_min_x;
extern uint16_t g_hid_min_y;
extern uint16_t g_hid_max_x;
extern uint16_t g_hid_max_y;
extern uint8_t g_hid_buttons_mask;

int ParseInt(const char* s);
void ResetHIDDecodeLearning();
bool PollHIDAndApply(uint8_t slot, uint32_t req_len, bool verbose, uint32_t timeout_iters = 3000000);
bool StartXHCIAutoMouse(uint32_t req_len, uint16_t mps, uint8_t interval);
void EnqueueAbsolutePointerEvent(int x, int y, int wheel, uint8_t buttons = 0);

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
        char t0[16];
        char t1[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhciintrin: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhciintrin: len must be 1..64");
            return true;
        }

        XHCIInterruptInResult rr{};
        if (!XHCIPollInterruptIn(g_xhci_caps, static_cast<uint8_t>(slot), static_cast<uint32_t>(req_len), &rr)) {
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

    if (StrEqual(cmd, "xhcihidpoll")) {
        if (!g_xhci_caps.valid) {
            console->PrintLine("xhcihidpoll: xhci not ready");
            return true;
        }
        int slot = g_last_xhci_slot_id;
        int req_len = 8;
        char t0[16];
        char t1[16];
        if (NextToken(command, &pos, t0, sizeof(t0))) {
            slot = ParseInt(t0);
        }
        if (NextToken(command, &pos, t1, sizeof(t1))) {
            req_len = ParseInt(t1);
        }
        if (slot <= 0 || slot > 255) {
            console->PrintLine("xhcihidpoll: invalid slot");
            return true;
        }
        if (req_len <= 0 || req_len > 64) {
            console->PrintLine("xhcihidpoll: len must be 1..64");
            return true;
        }

        PollHIDAndApply(static_cast<uint8_t>(slot), static_cast<uint32_t>(req_len), true);
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
        console->Print(" btn=0x");
        console->PrintHex(g_hid_buttons_mask, 2);
        console->Print(" auto_fail=");
        console->PrintDec(g_xhci_hid_auto_fail_count);
        console->Print(" auto_recover=");
        console->PrintDec(g_xhci_hid_auto_recover_count);
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
        g_xhci_hid_next_recover_tick = 0;
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

