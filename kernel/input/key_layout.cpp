#include "input/key_layout.hpp"

char KeycodeToAsciiByLayout(uint8_t keycode, bool shift, bool caps_lock, bool num_lock, bool jp_layout) {
    if (jp_layout) {
        switch (keycode) {
            case 0x02: return shift ? '!' : '1';
            case 0x03: return shift ? '"' : '2';
            case 0x04: return shift ? '#' : '3';
            case 0x05: return shift ? '$' : '4';
            case 0x06: return shift ? '%' : '5';
            case 0x07: return shift ? '&' : '6';
            case 0x08: return shift ? '\'' : '7';
            case 0x09: return shift ? '(' : '8';
            case 0x0A: return shift ? ')' : '9';
            case 0x0B: return shift ? ')' : '0';
            case 0x0C: return shift ? '=' : '-';
            case 0x0D: return shift ? '~' : '^';
            case 0x1A: return shift ? '`' : '@';
            case 0x1B: return shift ? '{' : '[';
            case 0x27: return shift ? '+' : ';';
            case 0x28: return shift ? '*' : ':';
            case 0x29: return shift ? '|' : '\\';
            case 0x2B: return shift ? '}' : ']';
            case 0x33: return shift ? '<' : ',';
            case 0x34: return shift ? '>' : '.';
            case 0x35: return shift ? '?' : '/';
            case 0x56: return shift ? '_' : '\\';
            case 0x73: return shift ? '_' : '\\';
            case 0x7D: return shift ? '|' : '\\';
            default: break;
        }
    }
    switch (keycode) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return (shift ^ caps_lock) ? 'Q' : 'q';
        case 0x11: return (shift ^ caps_lock) ? 'W' : 'w';
        case 0x12: return (shift ^ caps_lock) ? 'E' : 'e';
        case 0x13: return (shift ^ caps_lock) ? 'R' : 'r';
        case 0x14: return (shift ^ caps_lock) ? 'T' : 't';
        case 0x15: return (shift ^ caps_lock) ? 'Y' : 'y';
        case 0x16: return (shift ^ caps_lock) ? 'U' : 'u';
        case 0x17: return (shift ^ caps_lock) ? 'I' : 'i';
        case 0x18: return (shift ^ caps_lock) ? 'O' : 'o';
        case 0x19: return (shift ^ caps_lock) ? 'P' : 'p';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x1E: return (shift ^ caps_lock) ? 'A' : 'a';
        case 0x1F: return (shift ^ caps_lock) ? 'S' : 's';
        case 0x20: return (shift ^ caps_lock) ? 'D' : 'd';
        case 0x21: return (shift ^ caps_lock) ? 'F' : 'f';
        case 0x22: return (shift ^ caps_lock) ? 'G' : 'g';
        case 0x23: return (shift ^ caps_lock) ? 'H' : 'h';
        case 0x24: return (shift ^ caps_lock) ? 'J' : 'j';
        case 0x25: return (shift ^ caps_lock) ? 'K' : 'k';
        case 0x26: return (shift ^ caps_lock) ? 'L' : 'l';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x2C: return (shift ^ caps_lock) ? 'Z' : 'z';
        case 0x2D: return (shift ^ caps_lock) ? 'X' : 'x';
        case 0x2E: return (shift ^ caps_lock) ? 'C' : 'c';
        case 0x2F: return (shift ^ caps_lock) ? 'V' : 'v';
        case 0x30: return (shift ^ caps_lock) ? 'B' : 'b';
        case 0x31: return (shift ^ caps_lock) ? 'N' : 'n';
        case 0x32: return (shift ^ caps_lock) ? 'M' : 'm';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x56: return shift ? '>' : '<';
        case 0x37: return '*';  // keypad *
        case 0x4A: return '-';  // keypad -
        case 0x4E: return '+';  // keypad +
        case 0x53: return num_lock ? '.' : 0; // keypad .
        case 0x47: return num_lock ? '7' : 0; // keypad 7 / Home
        case 0x48: return num_lock ? '8' : 0; // keypad 8 / Up
        case 0x49: return num_lock ? '9' : 0; // keypad 9 / PgUp
        case 0x4B: return num_lock ? '4' : 0; // keypad 4 / Left
        case 0x4C: return num_lock ? '5' : 0; // keypad 5
        case 0x4D: return num_lock ? '6' : 0; // keypad 6 / Right
        case 0x4F: return num_lock ? '1' : 0; // keypad 1 / End
        case 0x50: return num_lock ? '2' : 0; // keypad 2 / Down
        case 0x51: return num_lock ? '3' : 0; // keypad 3 / PgDn
        case 0x52: return num_lock ? '0' : 0; // keypad 0 / Ins
        case 0x39: return ' ';
        case 0x1C: return '\n';
        default: return 0;
    }
}
