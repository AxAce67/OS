bool ExecuteHelpCommand() {
    console->PrintLine("help: core  help clear tick time mem uptime echo reboot");
    console->PrintLine("help: fs1   pwd cd mkdir touch write append cp");
    console->PrintLine("help: fs2   rm rmdir mv find grep ls stat cat");
    console->PrintLine("help: misc  history clearhistory inputstat about");
    console->PrintLine("help: cfg   repeat layout set alias xhciinfo xhciregs xhcistop xhcistart xhcireset xhciinit xhcienableslot xhciaddress xhciconfigep xhciintrin xhcihidpoll xhcihidstat xhciauto xhciautostart mouseabs usbports");
    return true;
}

bool ExecuteAboutCommand() {
    console->PrintLine("Native OS shell");
    console->PrintLine("arch: x86_64 / mode: kernel");
    return true;
}

bool ExecuteRepeatCommand(const char* rest) {
    if (rest[0] == '\0') {
        console->Print("repeat=");
        console->PrintLine(g_key_repeat_enabled ? "on" : "off");
        return true;
    }
    if (StrEqual(rest, "on")) {
        g_key_repeat_enabled = true;
        console->PrintLine("repeat=on");
        return true;
    }
    if (StrEqual(rest, "off")) {
        g_key_repeat_enabled = false;
        console->PrintLine("repeat=off");
        return true;
    }
    return false;
}

bool ExecuteLayoutCommand(const char* rest) {
    if (rest[0] == '\0') {
        console->Print("layout=");
        console->PrintLine(g_jp_layout ? "jp" : "us");
        return true;
    }
    if (StrEqual(rest, "us")) {
        g_jp_layout = false;
        console->PrintLine("layout=us");
        return true;
    }
    if (StrEqual(rest, "jp")) {
        g_jp_layout = true;
        console->PrintLine("layout=jp");
        return true;
    }
    return false;
}

bool ExecuteSetCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char key[32];
    if (!NextToken(command, &pos, key, sizeof(key))) {
        PrintPairs("vars:", g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])));
        console->Print("mouse.auto=");
        console->PrintLine(g_boot_mouse_auto_enabled ? "on" : "off");
        return true;
    }
    if (StrEqual(key, "mouse.auto")) {
        const char* v = RestOfLine(command, pos);
        if (v[0] == '\0') {
            console->Print("mouse.auto=");
            console->PrintLine(g_boot_mouse_auto_enabled ? "on" : "off");
            return true;
        }
        if (StrEqual(v, "on")) {
            g_boot_mouse_auto_enabled = true;
            console->PrintLine("mouse.auto=on");
            return true;
        }
        if (StrEqual(v, "off")) {
            g_boot_mouse_auto_enabled = false;
            console->PrintLine("mouse.auto=off");
            return true;
        }
        console->PrintLine("set mouse.auto: use on/off");
        return true;
    }
    ShellPair* p = EnsurePair(g_vars, static_cast<int>(sizeof(g_vars) / sizeof(g_vars[0])), key);
    if (p == nullptr) {
        console->PrintLine("set: table full");
        return true;
    }
    CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
    console->Print("set ");
    console->Print(p->key);
    console->Print("=");
    console->PrintLine(p->value);
    return true;
}

bool ExecuteAliasCommand(const char* command, int* pos_ptr) {
    int& pos = *pos_ptr;
    char key[32];
    if (!NextToken(command, &pos, key, sizeof(key))) {
        PrintPairs("aliases:", g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])));
        return true;
    }
    ShellPair* p = EnsurePair(g_aliases, static_cast<int>(sizeof(g_aliases) / sizeof(g_aliases[0])), key);
    if (p == nullptr) {
        console->PrintLine("alias: table full");
        return true;
    }
    CopyString(p->value, RestOfLine(command, pos), sizeof(p->value));
    console->Print("alias ");
    console->Print(p->key);
    console->Print("=");
    console->PrintLine(p->value);
    return true;
}

bool ExecuteEchoCommand(const char* rest) {
    console->PrintLine(rest);
    return true;
}

bool ExecuteRebootCommand() {
    console->PrintLine("rebooting...");
    Reboot();
    return true;
}

bool ExecutePwdCommand() {
    console->PrintLine(g_cwd);
    return true;
}

bool ExecuteClearCommand() {
    console->Clear();
    return true;
}

bool ExecuteTickTimeCommand() {
    console->Print("ticks=");
    console->PrintDec(static_cast<int64_t>(CurrentTick()));
    console->Print("\n");
    return true;
}

bool ExecuteUptimeCommand() {
    console->Print("uptime_ticks=");
    console->PrintDec(static_cast<int64_t>(CurrentTick()));
    console->Print("\n");
    return true;
}

bool ExecuteMemCommand() {
    uint64_t free_pages = memory_manager->CountFreePages();
    uint64_t free_mib = (free_pages * kPageSize) / kMiB;
    console->Print("free_ram=");
    console->PrintDec(static_cast<int64_t>(free_mib));
    console->PrintLine(" MiB");
    return true;
}

bool ExecuteInputStatCommand() {
    console->Print("kbd_dropped=");
    console->PrintDec(static_cast<int64_t>(g_keyboard_dropped_events));
    console->Print(" mouse_dropped=");
    console->PrintDec(static_cast<int64_t>(g_mouse_dropped_events));
    console->Print(" btn=0x");
    console->PrintHex(g_mouse_buttons_current, 2);
    console->Print(" l=");
    console->PrintDec(static_cast<int64_t>(g_mouse_left_press_count));
    console->Print(" r=");
    console->PrintDec(static_cast<int64_t>(g_mouse_right_press_count));
    console->Print(" m=");
    console->PrintDec(static_cast<int64_t>(g_mouse_middle_press_count));
    console->Print("\n");
    return true;
}

