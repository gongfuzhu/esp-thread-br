#include "bridge_eui64.h"
#include <string.h>

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool eui64_from_string(const char *s, uint8_t out[8]) {
    if (s == NULL || strlen(s) != 16) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        int hi = hex_val(s[i * 2]);
        int lo = hex_val(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void eui64_to_string(const uint8_t in[8], char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = hexd[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hexd[in[i] & 0xf];
    }
    out[16] = '\0';
}
