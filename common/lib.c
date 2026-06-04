#include "lib.h"

void u64_to_dec(uint64_t v, char* buf) {
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
        buf[n] = 0;
        return;
    }
    char tmp[32];
    int t = 0;
    while (v) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (t--) buf[n++] = tmp[t];
    buf[n] = 0;
}

void u32_to_dec(uint32_t v, char* buf) {
    u64_to_dec((uint64_t)v, buf);
}

void u64_to_hex(uint64_t v, char* buf) {
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
        buf[n] = 0;
        return;
    }
    char tmp[32];
    int t = 0;
    while (v) {
        tmp[t++] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }
    while (t--) buf[n++] = tmp[t];
    buf[n] = 0;
}

void u32_to_hex(uint32_t v, char* buf) {
    u64_to_hex((uint64_t)v, buf);
}

int str_len(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

int streq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

int strcaseeq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

int startswith(const char* s, const char* p) {
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

void str_copy(char* dst, const char* src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

uint64_t dec_to_u64(const char* s) {
    uint64_t res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}
