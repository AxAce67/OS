#pragma once

#include <stdint.h>

struct ShellPair {
    bool used;
    char key[32];
    char value[96];
};

struct ShellDir {
    bool used;
    char path[96];
};

struct ShellFile {
    bool used;
    char path[96];
    uint64_t size;
    uint8_t data[2048];
};

