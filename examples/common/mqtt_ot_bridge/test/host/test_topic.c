#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../src/bridge_topic.h"

int main(void) {
    bridge_cmd_t c;

    assert(bridge_topic_parse("unicast/1a2b3c4d5e6f7080", &c));
    assert(c.kind == BRIDGE_CMD_UNICAST);
    assert(c.eui64[0] == 0x1a && c.eui64[7] == 0x80);

    assert(bridge_topic_parse("multicast", &c));
    assert(c.kind == BRIDGE_CMD_MULTICAST);

    // 未知前缀
    assert(!bridge_topic_parse("foobar", &c));
    // unicast 但 EUI64 非法
    assert(!bridge_topic_parse("unicast/xyz", &c));
    // unicast 缺少 eui64
    assert(!bridge_topic_parse("unicast/", &c));
    assert(!bridge_topic_parse("unicast", &c));

    printf("test_topic OK\n");
    return 0;
}
