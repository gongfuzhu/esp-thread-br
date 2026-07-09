#include "device_eui64.h"

void device_eui64_to_string(const uint8_t in[8], char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = hexd[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hexd[in[i] & 0xf];
    }
    out[16] = '\0';
}