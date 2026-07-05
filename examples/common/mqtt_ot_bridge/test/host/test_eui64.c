#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../src/bridge_eui64.h"

int main(void) {
    uint8_t b[8];

    // 合法：小写
    assert(eui64_from_string("1a2b3c4d5e6f7080", b));
    uint8_t expect[8] = {0x1a,0x2b,0x3c,0x4d,0x5e,0x6f,0x70,0x80};
    assert(memcmp(b, expect, 8) == 0);

    // 合法：大写也接受
    assert(eui64_from_string("AABBCCDDEEFF0011", b));
    assert(b[0] == 0xAA && b[7] == 0x11);

    // 非法：长度不对
    assert(!eui64_from_string("1a2b", b));
    assert(!eui64_from_string("1a2b3c4d5e6f708090", b));
    // 非法：非十六进制字符
    assert(!eui64_from_string("1a2b3c4d5e6f70gz", b));

    // 往返
    char s[17];
    eui64_to_string(expect, s);
    assert(strcmp(s, "1a2b3c4d5e6f7080") == 0);

    printf("test_eui64 OK\n");
    return 0;
}
