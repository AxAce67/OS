#include "shell/text.hpp"

bool StrEqual(const char* a, const char* b) {
    for (int i = 0;; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

bool StrStartsWith(const char* s, const char* prefix) {
    for (int i = 0; prefix[i] != '\0'; ++i) {
        if (s[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

int StrCompare(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        }
        ++i;
    }
    return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
}

bool ContainsChar(const char* s, char ch) {
    for (int i = 0; s[i] != '\0'; ++i) {
        if (s[i] == ch) {
            return true;
        }
    }
    return false;
}

bool StrContains(const char* haystack, const char* needle) {
    if (needle[0] == '\0') {
        return true;
    }
    int hlen = 0;
    while (haystack[hlen] != '\0') {
        ++hlen;
    }
    int nlen = 0;
    while (needle[nlen] != '\0') {
        ++nlen;
    }
    if (nlen > hlen) {
        return false;
    }
    for (int i = 0; i <= hlen - nlen; ++i) {
        bool ok = true;
        for (int j = 0; j < nlen; ++j) {
            if (haystack[i + j] != needle[j]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

int StrLength(const char* s) {
    int len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

void CopyString(char* dst, const char* src, int max_len) {
    if (max_len <= 0) {
        return;
    }
    int i = 0;
    for (; i < max_len - 1 && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

const char* SkipSpaces(const char* s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

bool NextToken(const char* s, int* pos, char* out, int out_len) {
    int p = *pos;
    while (s[p] == ' ') {
        ++p;
    }
    if (s[p] == '\0') {
        out[0] = '\0';
        *pos = p;
        return false;
    }
    int w = 0;
    while (s[p] != '\0' && s[p] != ' ' && w + 1 < out_len) {
        out[w++] = s[p++];
    }
    out[w] = '\0';
    while (s[p] != '\0' && s[p] != ' ') {
        ++p;
    }
    *pos = p;
    return true;
}

const char* RestOfLine(const char* s, int pos) {
    return SkipSpaces(s + pos);
}
