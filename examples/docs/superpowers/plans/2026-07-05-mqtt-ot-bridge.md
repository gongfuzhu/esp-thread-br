# MQTT ↔ Thread CoAP Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 ESP Thread BR 主机固件上新增一个无状态的 MQTT↔CoAP 桥接组件,让局域网 MQTT 客户端经 BR 单播/组播控制自研 ESP32-H2 IoT 设备,并周期上报 SRP 注册的设备清单。

**Architecture:** BR 是无状态双向管道。下行:MQTT `cmd/unicast/<eui64>` → CoAP CON 单播;`cmd/multicast` → CoAP NON 到 `ff03::1`。上行:CON 事务响应回调 + BR 自建 `/ack` CoAP 资源两条路径,均把 payload 原样发布到 `dev/response`。设备发现:遍历 SRP server 表得到 `{eui64, ipv6, service}`,既用于 `dev/registry`(retained)清单上报,也用于单播 `EUI64→IPv6` 选路。BR 不解析 payload、不生成 reqid、不对账、不分组——对账由服务端凭自带 reqid 完成。

**Tech Stack:** ESP-IDF v6.0.2、OpenThread(`otCoap*`/`otSrpServer*` API,IDF 内置)、esp-mqtt(`espressif/mqtt` 托管组件)、cJSON(IDF 内置)。纯逻辑单测用 host `gcc`。

## Global Constraints

- ESP-IDF 环境:`source /d/esp/v6.0.2/esp-idf/export.sh`,IDF 版本 **v6.0.2**(非上游 README 的 v5.5.4,注意 Kconfig/API 差异)。
- **`idf.py` 必须在 ESP-IDF PowerShell/CMD 中运行,不能在 Git Bash(MSys/Mingw)里跑**(会报 "MSys/Mingw is no longer supported")。
- **IDF v6 托管组件名(实现期核实)**:esp-mqtt 与 cJSON 不再是内置组件 `mqtt`/`json`,均已迁移到组件管理器:
  - esp-mqtt:依赖声明 `espressif/mqtt`,CMake `REQUIRES` 名 `espressif__mqtt`,头 `#include "mqtt_client.h"`。
  - cJSON:依赖声明 `espressif/cjson`,CMake `REQUIRES` 名 `espressif__cjson`,头 `#include "cJSON.h"`。
  - 组件若要用托管依赖,需在**自己**的 `idf_component.yml` 声明,并在 CMakeLists 用 `namespace__name` 形式列入 (PRIV_)REQUIRES。
- **无主机端测试套件**:硬件粘合代码验证方式 = `idf.py build` + 设备端 `monitor` 观察预期日志。纯逻辑模块(EUI64/topic/JSON 解析)拆为**不 include 任何 OpenThread/ESP 头**的独立 `.c`,用 host `gcc` 跑真实 TDD(注:本机无 host gcc/make,纯逻辑靠代码审查 + 固件编译覆盖)。
- **不要手改 `sdkconfig`**(自动生成);配置项放 `Kconfig.projbuild`,目标覆盖放 `sdkconfig.defaults.<target>`。
- 组件目录:`common/mqtt_ot_bridge`,组织方式参照 `common/thread_border_router`(`src/` + `include/` + `Kconfig.projbuild` + `CMakeLists.txt` 用 `idf_component_register`)。
- 所有 OpenThread API 调用必须持锁:`esp_openthread_lock_acquire(portMAX_DELAY)` … `esp_openthread_lock_release()`。CoAP 回调在 OT 任务上下文中执行(已持锁),回调内**不可**再次 acquire。
- BR 无状态铁律:不记已发命令、不建待办表、不跑对账定时器、不解析 payload 语义、不生成/读取 reqid(reqid 在 payload 内,对 BR 透明)。
- EUI64 文本格式:16 位小写十六进制,无分隔符(如 `1a2b3c4d5e6f7080`),与设备 SRP host/instance 命名约定一致。
- 组播地址默认 `ff03::1`(realm-local all-nodes),可经 Kconfig 覆盖。
- 全部 Kconfig 项挂在 `menu "MQTT OT Bridge"`,顶层开关 `MQTT_OT_BRIDGE_ENABLE`(默认 n),关闭时 BR 行为与现状完全一致。

---

## File Structure

```
common/mqtt_ot_bridge/
├── CMakeLists.txt                     # idf_component_register，REQUIRES openthread mqtt json 等
├── Kconfig.projbuild                  # menu "MQTT OT Bridge" 全部配置项
├── include/
│   └── mqtt_ot_bridge.h               # 公共 API: start/stop
├── src/
│   ├── mqtt_ot_bridge.c               # 顶层编排：MQTT 事件 → 分发；启动 CoAP、注册 /ack、清单定时器
│   ├── bridge_eui64.c / .h            # 纯逻辑：EUI64 文本 <-> 字节；无 ESP/OT 头
│   ├── bridge_topic.c / .h            # 纯逻辑：解析 cmd/unicast/<eui64> 与 cmd/multicast；无 ESP/OT 头
│   └── bridge_registry_json.c / .h    # 纯逻辑：{eui64,ipv6,service}[] -> JSON 字符串；仅依赖 cJSON
└── test/host/                         # host gcc 单测（不进固件构建）
    ├── Makefile
    ├── test_eui64.c
    ├── test_topic.c
    └── test_registry_json.c
```

纯逻辑三件套(eui64 / topic / registry_json)与 OT 隔离,是唯一能真单测的部分,故优先实现。`mqtt_ot_bridge.c` 消费它们并接 OT/MQTT,靠编译+monitor 验证。

---

## Task 1: 组件脚手架 + EUI64 纯逻辑模块(host TDD)

**Files:**
- Create: `common/mqtt_ot_bridge/CMakeLists.txt`
- Create: `common/mqtt_ot_bridge/include/mqtt_ot_bridge.h`
- Create: `common/mqtt_ot_bridge/src/bridge_eui64.h`
- Create: `common/mqtt_ot_bridge/src/bridge_eui64.c`
- Create: `common/mqtt_ot_bridge/test/host/Makefile`
- Test: `common/mqtt_ot_bridge/test/host/test_eui64.c`

**Interfaces:**
- Produces: `bool eui64_from_string(const char *s, uint8_t out[8]);` — 成功返回 true,`s` 必须恰为 16 位十六进制;`out` 填 8 字节大端(文本首字节→out[0])。
- Produces: `void eui64_to_string(const uint8_t in[8], char out[17]);` — 输出 16 位小写十六进制 + `\0`。
- Produces: `mqtt_ot_bridge.h` 中 `esp_err_t mqtt_ot_bridge_start(void);` 与 `void mqtt_ot_bridge_stop(void);`(本任务仅声明)。

- [ ] **Step 1: 写失败测试**

`common/mqtt_ot_bridge/test/host/test_eui64.c`:
```c
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
```

`common/mqtt_ot_bridge/test/host/Makefile`:
```make
CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g
SRC = ../../src

.PHONY: all clean
all: test_eui64 test_topic test_registry_json
	./test_eui64
	./test_topic
	./test_registry_json

test_eui64: test_eui64.c $(SRC)/bridge_eui64.c
	$(CC) $(CFLAGS) -o $@ $^

test_topic: test_topic.c $(SRC)/bridge_topic.c $(SRC)/bridge_eui64.c
	$(CC) $(CFLAGS) -o $@ $^

test_registry_json: test_registry_json.c $(SRC)/bridge_registry_json.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libcjson 2>/dev/null) -o $@ $^ $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)

clean:
	rm -f test_eui64 test_topic test_registry_json
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd common/mqtt_ot_bridge/test/host && make test_eui64`
Expected: 编译失败 —— `bridge_eui64.h: No such file` 或 `undefined reference to eui64_from_string`。

- [ ] **Step 3: 写最小实现**

`common/mqtt_ot_bridge/src/bridge_eui64.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// 16位十六进制文本(无分隔符) -> 8字节，首字节对应 out[0]。成功返回 true。
bool eui64_from_string(const char *s, uint8_t out[8]);
// 8字节 -> 16位小写十六进制 + '\0'(out 至少 17 字节)。
void eui64_to_string(const uint8_t in[8], char out[17]);
```

`common/mqtt_ot_bridge/src/bridge_eui64.c`:
```c
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
```

`common/mqtt_ot_bridge/include/mqtt_ot_bridge.h`:
```c
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动 MQTT↔CoAP 桥接：连接 broker、启动 CoAP、注册 /ack、启动清单定时器。
// 必须在 OpenThread/BR 已启动之后调用。
esp_err_t mqtt_ot_bridge_start(void);

// 停止桥接(释放 MQTT 客户端与定时器)。
void mqtt_ot_bridge_stop(void);

#ifdef __cplusplus
}
#endif
```

`common/mqtt_ot_bridge/CMakeLists.txt`:
```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES openthread
                       PRIV_REQUIRES mqtt json esp_netif)
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd common/mqtt_ot_bridge/test/host && make test_eui64`
Expected: `test_eui64 OK`。

- [ ] **Step 5: 提交**

```bash
git add common/mqtt_ot_bridge/CMakeLists.txt common/mqtt_ot_bridge/include common/mqtt_ot_bridge/src/bridge_eui64.* common/mqtt_ot_bridge/test/host/Makefile common/mqtt_ot_bridge/test/host/test_eui64.c
git commit -m "feat(mqtt_ot_bridge): scaffold component + EUI64 parse/format with host tests"
```

---

## Task 2: Topic 解析纯逻辑模块(host TDD)

**Files:**
- Create: `common/mqtt_ot_bridge/src/bridge_topic.h`
- Create: `common/mqtt_ot_bridge/src/bridge_topic.c`
- Test: `common/mqtt_ot_bridge/test/host/test_topic.c`

**Interfaces:**
- Consumes: `eui64_from_string` (Task 1)。
- Produces:
```c
typedef enum { BRIDGE_CMD_NONE = 0, BRIDGE_CMD_UNICAST, BRIDGE_CMD_MULTICAST } bridge_cmd_kind_t;
typedef struct { bridge_cmd_kind_t kind; uint8_t eui64[8]; } bridge_cmd_t;
// 解析去掉前缀后的 topic 尾部。返回是否为已知命令。
bool bridge_topic_parse(const char *topic_suffix, bridge_cmd_t *out);
```
  约定:`topic_suffix` 是去掉可配置前缀后的部分,即 `unicast/<eui64>` 或 `multicast`。

- [ ] **Step 1: 写失败测试**

`common/mqtt_ot_bridge/test/host/test_topic.c`:
```c
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
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd common/mqtt_ot_bridge/test/host && make test_topic`
Expected: 编译失败 —— `bridge_topic.h: No such file`。

- [ ] **Step 3: 写最小实现**

`common/mqtt_ot_bridge/src/bridge_topic.h`:
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BRIDGE_CMD_NONE = 0,
    BRIDGE_CMD_UNICAST,
    BRIDGE_CMD_MULTICAST,
} bridge_cmd_kind_t;

typedef struct {
    bridge_cmd_kind_t kind;
    uint8_t eui64[8];   // 仅 UNICAST 有效
} bridge_cmd_t;

// 解析去掉可配置前缀后的 topic 尾部：期望 "unicast/<eui64>" 或 "multicast"。
bool bridge_topic_parse(const char *topic_suffix, bridge_cmd_t *out);
```

`common/mqtt_ot_bridge/src/bridge_topic.c`:
```c
#include "bridge_topic.h"
#include "bridge_eui64.h"
#include <string.h>

#define UNICAST_PREFIX "unicast/"

bool bridge_topic_parse(const char *topic_suffix, bridge_cmd_t *out) {
    if (topic_suffix == NULL || out == NULL) {
        return false;
    }
    out->kind = BRIDGE_CMD_NONE;

    if (strcmp(topic_suffix, "multicast") == 0) {
        out->kind = BRIDGE_CMD_MULTICAST;
        return true;
    }
    size_t plen = strlen(UNICAST_PREFIX);
    if (strncmp(topic_suffix, UNICAST_PREFIX, plen) == 0) {
        const char *id = topic_suffix + plen;
        if (eui64_from_string(id, out->eui64)) {
            out->kind = BRIDGE_CMD_UNICAST;
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd common/mqtt_ot_bridge/test/host && make test_topic`
Expected: `test_topic OK`。

- [ ] **Step 5: 提交**

```bash
git add common/mqtt_ot_bridge/src/bridge_topic.* common/mqtt_ot_bridge/test/host/test_topic.c
git commit -m "feat(mqtt_ot_bridge): parse downlink topics into unicast/multicast commands"
```

---

## Task 3: Registry JSON 序列化纯逻辑模块(host TDD)

**Files:**
- Create: `common/mqtt_ot_bridge/src/bridge_registry_json.h`
- Create: `common/mqtt_ot_bridge/src/bridge_registry_json.c`
- Test: `common/mqtt_ot_bridge/test/host/test_registry_json.c`

**Interfaces:**
- Produces:
```c
typedef struct {
    char eui64[17];      // 16 hex + '\0'
    char ipv6[46];       // 文本 IPv6，最长 45 + '\0'
    char service[64];    // 服务实例名(可空串)
} bridge_dev_entry_t;
// 生成 JSON 数组字符串；调用者用 free() 释放。失败返回 NULL。
char *bridge_registry_to_json(const bridge_dev_entry_t *entries, size_t count);
```

- [ ] **Step 1: 写失败测试**

`common/mqtt_ot_bridge/test/host/test_registry_json.c`:
```c
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "../../src/bridge_registry_json.h"

int main(void) {
    bridge_dev_entry_t e[2] = {
        { "1a2b3c4d5e6f7080", "fd00::1", "light01" },
        { "aabbccddeeff0011", "fd00::2", "" },
    };
    char *js = bridge_registry_to_json(e, 2);
    assert(js != NULL);

    cJSON *root = cJSON_Parse(js);
    assert(cJSON_IsArray(root));
    assert(cJSON_GetArraySize(root) == 2);

    cJSON *first = cJSON_GetArrayItem(root, 0);
    assert(strcmp(cJSON_GetObjectItem(first, "eui64")->valuestring, "1a2b3c4d5e6f7080") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "ipv6")->valuestring, "fd00::1") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "service")->valuestring, "light01") == 0);

    // 空数组
    char *empty = bridge_registry_to_json(NULL, 0);
    assert(empty != NULL);
    cJSON *er = cJSON_Parse(empty);
    assert(cJSON_IsArray(er) && cJSON_GetArraySize(er) == 0);

    cJSON_Delete(root); cJSON_Delete(er);
    free(js); free(empty);
    printf("test_registry_json OK\n");
    return 0;
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd common/mqtt_ot_bridge/test/host && make test_registry_json`
Expected: 编译失败 —— `bridge_registry_json.h: No such file`。
(前置:host 需装 libcjson,如 `sudo apt install libcjson-dev`;若不可用见下方备注。)

- [ ] **Step 3: 写最小实现**

`common/mqtt_ot_bridge/src/bridge_registry_json.h`:
```c
#pragma once
#include <stddef.h>

typedef struct {
    char eui64[17];
    char ipv6[46];
    char service[64];
} bridge_dev_entry_t;

// 生成 JSON 数组字符串(调用者 free)。count 可为 0(entries 可 NULL)。失败返回 NULL。
char *bridge_registry_to_json(const bridge_dev_entry_t *entries, size_t count);
```

`common/mqtt_ot_bridge/src/bridge_registry_json.c`:
```c
#include "bridge_registry_json.h"
#include "cJSON.h"

char *bridge_registry_to_json(const bridge_dev_entry_t *entries, size_t count) {
    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            cJSON_Delete(arr);
            return NULL;
        }
        cJSON_AddStringToObject(o, "eui64", entries[i].eui64);
        cJSON_AddStringToObject(o, "ipv6", entries[i].ipv6);
        cJSON_AddStringToObject(o, "service", entries[i].service);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd common/mqtt_ot_bridge/test/host && make`
Expected: `test_eui64 OK` / `test_topic OK` / `test_registry_json OK` 三行全部打印。
备注:若 host 无 libcjson,跳过本测试的运行(`make test_eui64 test_topic`),固件构建时 IDF 的 `json` 组件自带 cJSON,序列化实现不变。

- [ ] **Step 5: 提交**

```bash
git add common/mqtt_ot_bridge/src/bridge_registry_json.* common/mqtt_ot_bridge/test/host/test_registry_json.c common/mqtt_ot_bridge/test/host/Makefile
git commit -m "feat(mqtt_ot_bridge): serialize device registry to JSON array"
```

---

## Task 4: Kconfig 配置项

**Files:**
- Create: `common/mqtt_ot_bridge/Kconfig.projbuild`

**Interfaces:**
- Produces(供后续任务通过 `sdkconfig.h` 消费): `CONFIG_MQTT_OT_BRIDGE_ENABLE`、`CONFIG_MQTT_OT_BRIDGE_BROKER_URI`、`CONFIG_MQTT_OT_BRIDGE_USERNAME`、`CONFIG_MQTT_OT_BRIDGE_PASSWORD`、`CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX`、`CONFIG_MQTT_OT_BRIDGE_MULTICAST_ADDR`、`CONFIG_MQTT_OT_BRIDGE_COAP_URI`、`CONFIG_MQTT_OT_BRIDGE_ACK_URI`、`CONFIG_MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S`、`CONFIG_MQTT_OT_BRIDGE_COAP_PORT`。

- [ ] **Step 1: 写 Kconfig**

`common/mqtt_ot_bridge/Kconfig.projbuild`:
```kconfig
menu "MQTT OT Bridge"

    config MQTT_OT_BRIDGE_ENABLE
        bool "Enable MQTT to OpenThread CoAP bridge"
        default n
        help
            启用后，BR 连接局域网 MQTT broker，把下行命令翻译为 CoAP 请求，
            并把设备响应与 SRP 设备清单发布回 MQTT。

    if MQTT_OT_BRIDGE_ENABLE

        config MQTT_OT_BRIDGE_BROKER_URI
            string "MQTT broker URI"
            default "mqtt://192.168.1.100:1883"
            help
                局域网 broker 地址，形如 mqtt://host:port(不使用 TLS)。

        config MQTT_OT_BRIDGE_USERNAME
            string "MQTT username"
            default "bridge"

        config MQTT_OT_BRIDGE_PASSWORD
            string "MQTT password"
            default "changeme"

        config MQTT_OT_BRIDGE_TOPIC_PREFIX
            string "MQTT topic prefix"
            default "otbr"
            help
                所有 topic 的前缀，如 otbr/cmd/unicast/<eui64>。

        config MQTT_OT_BRIDGE_MULTICAST_ADDR
            string "CoAP multicast address"
            default "ff03::1"

        config MQTT_OT_BRIDGE_COAP_URI
            string "CoAP downlink resource path on devices"
            default "ctrl"
            help
                下行命令发往设备的 CoAP Uri-Path(不含前导斜杠)。

        config MQTT_OT_BRIDGE_ACK_URI
            string "CoAP uplink resource path on BR"
            default "ack"
            help
                BR 侧接收设备主动上报的 CoAP 资源 Uri-Path。

        config MQTT_OT_BRIDGE_COAP_PORT
            int "CoAP port"
            default 5683

        config MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S
            int "Device registry publish interval (seconds)"
            default 30
            range 5 3600

    endif

endmenu
```

- [ ] **Step 2: 验证 Kconfig 语法(用示例工程解析)**

Run: `source /d/esp/v6.0.2/esp-idf/export.sh && cd basic_thread_border_router && idf.py reconfigure 2>&1 | tail -20`
Expected: reconfigure 成功;由于组件尚未被示例引用,此步仅验证仓库无语法破坏(下一任务接入后 menuconfig 才会出现该菜单)。若 reconfigure 报与本组件无关的已存在告警,可忽略。

- [ ] **Step 3: 提交**

```bash
git add common/mqtt_ot_bridge/Kconfig.projbuild
git commit -m "feat(mqtt_ot_bridge): add Kconfig options for broker/topics/coap/registry"
```

---

## Task 5: MQTT 客户端 + 顶层编排骨架(编译验证)

**Files:**
- Create: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`
- Modify: `basic_thread_border_router/main/idf_component.yml`(加 `espressif/mqtt` 依赖)
- Modify: `basic_thread_border_router/main/CMakeLists.txt`(把组件加入 REQUIRES,如该文件用显式 REQUIRES)
- Modify: `basic_thread_border_router/main/esp_ot_br.c`(BR 启动后调用)

**Interfaces:**
- Consumes: `bridge_topic_parse`/`bridge_cmd_t`(Task 2)、`mqtt_ot_bridge_start/stop`(Task 1 声明)。
- Produces(文件内静态,供 Task 6/7 填充): `static void handle_downlink(const char *topic, const char *data, int data_len);`(MQTT DATA 事件分发入口)、`static esp_mqtt_client_handle_t s_client;`、`static void publish_uplink(const char *payload, int len);`。

- [ ] **Step 1: 写 MQTT 骨架实现**

`common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`:
```c
#include "mqtt_ot_bridge.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "bridge_topic.h"

#define TAG "mqtt_ot_bridge"

static esp_mqtt_client_handle_t s_client = NULL;

// 由 Task 6 实现：把 payload 透传发布到 <prefix>/dev/response
static void publish_uplink(const char *payload, int len);
// 由 Task 6/7 实现：解析 topic 尾部并触发 CoAP。
static void handle_downlink(const char *topic, const char *data, int data_len);

static const char *topic_suffix_after_prefix(const char *topic) {
    // topic 形如 "<prefix>/cmd/unicast/<eui64>"，返回 "unicast/..."/"multicast"
    const char *cmd = strstr(topic, "/cmd/");
    if (cmd == NULL) {
        return NULL;
    }
    return cmd + strlen("/cmd/");
}

static void subscribe_all(esp_mqtt_client_handle_t client) {
    char t[128];
    snprintf(t, sizeof(t), "%s/cmd/unicast/+", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
    snprintf(t, sizeof(t), "%s/cmd/multicast", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
    ESP_LOGI(TAG, "subscribed under prefix '%s'", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        subscribe_all(event->client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, will retry");
        break;
    case MQTT_EVENT_DATA: {
        char topic[128];
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = '\0';
        const char *suffix = topic_suffix_after_prefix(topic);
        if (suffix != NULL) {
            handle_downlink(suffix, event->data, event->data_len);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static void publish_uplink(const char *payload, int len) {
    if (s_client == NULL) {
        return;
    }
    char t[128];
    snprintf(t, sizeof(t), "%s/dev/response", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_publish(s_client, t, payload, len, 0, 0);
}

// Task 7 会替换为真正的 CoAP 逻辑;此处先留桩以便独立编译。
static void handle_downlink(const char *topic_suffix, const char *data, int data_len) {
    bridge_cmd_t cmd;
    if (!bridge_topic_parse(topic_suffix, &cmd)) {
        ESP_LOGW(TAG, "unknown downlink topic suffix: %s", topic_suffix);
        return;
    }
    ESP_LOGI(TAG, "downlink kind=%d len=%d (coap wired in Task 7)", cmd.kind, data_len);
    (void)data;
}

esp_err_t mqtt_ot_bridge_start(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_OT_BRIDGE_BROKER_URI,
        .credentials.username = CONFIG_MQTT_OT_BRIDGE_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_OT_BRIDGE_PASSWORD,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

void mqtt_ot_bridge_stop(void) {
    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}
```

- [ ] **Step 2: 接入示例工程依赖**

`basic_thread_border_router/main/idf_component.yml` 增加(在 `dependencies:` 下,保留既有项):
```yaml
  espressif/mqtt: "*"
```
说明:esp-mqtt 是 IDF 内置组件,`mqtt` 亦可直接作为 `REQUIRES`;声明 `espressif/mqtt` 依赖可确保托管拉取。若 `idf_component.yml` 无 `dependencies:` 段,则新建该段。

- [ ] **Step 3: 让 main 引用桥接组件**

查看 `basic_thread_border_router/main/CMakeLists.txt`;若使用显式 `REQUIRES`/`PRIV_REQUIRES` 列表,追加 `mqtt_ot_bridge`。同时在 `basic_thread_border_router/CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 需包含 `../common`(现有示例已包含 common,确认即可)。

- [ ] **Step 4: 在 BR 启动后调用桥接**

修改 `basic_thread_border_router/main/esp_ot_br.c`:在文件顶部包含区加入
```c
#include "mqtt_ot_bridge.h"
```
在 `app_main()` 末尾 `launch_openthread_border_router(...)` 之后加入:
```c
#if CONFIG_MQTT_OT_BRIDGE_ENABLE
    ESP_ERROR_CHECK(mqtt_ot_bridge_start());
#endif
```

- [ ] **Step 5: 编译验证**

Run:
```bash
source /d/esp/v6.0.2/esp-idf/export.sh
cd basic_thread_border_router
idf.py build 2>&1 | tail -30
```
先执行 `idf.py menuconfig` → `MQTT OT Bridge` → 勾选 `Enable`(或临时在 `sdkconfig.defaults` 加 `CONFIG_MQTT_OT_BRIDGE_ENABLE=y` 后 `idf.py reconfigure`)。
Expected: 编译成功,产物含 `mqtt_ot_bridge` 组件;`mqtt_client.h` 能被找到。若报 `mqtt_client.h not found`,确认 `PRIV_REQUIRES mqtt` 生效。

- [ ] **Step 6: 提交**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c basic_thread_border_router/main/idf_component.yml basic_thread_border_router/main/CMakeLists.txt basic_thread_border_router/main/esp_ot_br.c
git commit -m "feat(mqtt_ot_bridge): MQTT client + downlink dispatch skeleton, wired into BR startup"
```

---

## Task 6: CoAP 启动、/ack 资源、上行透传(编译 + monitor 验证)

**Files:**
- Modify: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`

**Interfaces:**
- Consumes: `publish_uplink`(Task 5)、`esp_openthread_get_instance`、`otCoapStart`、`otCoapAddResource`、`otMessageGetLength`/`otMessageRead`。
- Produces: `static bool s_coap_started;`、`static otCoapResource s_ack_resource;`、`static void ack_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info);`、`static int coap_read_payload(otMessage *msg, char *buf, int buf_size);`(供 Task 7 复用读取响应体)。

- [ ] **Step 1: 增加 CoAP 头文件与 payload 读取工具**

在 `mqtt_ot_bridge.c` 顶部包含区追加:
```c
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"
```
在 `publish_uplink` 之后加入 payload 读取工具:
```c
// 从 CoAP message 读取 payload 到 buf(不含 CoAP 头)。返回读取字节数。
static int coap_read_payload(otMessage *msg, char *buf, int buf_size) {
    uint16_t offset = otMessageGetOffset(msg);
    uint16_t total  = otMessageGetLength(msg);
    int payload_len = (int)total - (int)offset;
    if (payload_len < 0) {
        payload_len = 0;
    }
    if (payload_len > buf_size - 1) {
        payload_len = buf_size - 1;
    }
    int read = otMessageRead(msg, offset, buf, payload_len);
    buf[read] = '\0';
    return read;
}
```

- [ ] **Step 2: 实现 /ack 资源 handler 与 CoAP 启动**

在 `handle_downlink` 之前加入:
```c
static bool s_coap_started = false;

static void ack_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx; (void)info;
    char payload[512];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    ESP_LOGI(TAG, "/%s got %d bytes -> uplink", CONFIG_MQTT_OT_BRIDGE_ACK_URI, len);
    publish_uplink(payload, len);
    // 设备用 NON 上报时无需 ACK;若为 CON,OpenThread 视资源需要可回空 ACK，这里从简不回。
}

static otCoapResource s_ack_resource = {
    .mUriPath = CONFIG_MQTT_OT_BRIDGE_ACK_URI,
    .mHandler = ack_request_handler,
    .mContext = NULL,
    .mNext = NULL,
};

static esp_err_t coap_ensure_started(void) {
    if (s_coap_started) {
        return ESP_OK;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    otError err = otCoapStart(inst, CONFIG_MQTT_OT_BRIDGE_COAP_PORT);
    if (err == OT_ERROR_NONE) {
        otCoapAddResource(inst, &s_ack_resource);
        s_coap_started = true;
    }
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "coap start err=%d", err);
    return err == OT_ERROR_NONE ? ESP_OK : ESP_FAIL;
}
```
在 `freertos/FreeRTOS.h` 尚未包含时,顶部追加 `#include "freertos/FreeRTOS.h"` 与 `#include "freertos/task.h"`(为 `portMAX_DELAY`)。

- [ ] **Step 3: 在 start 流程里启动 CoAP**

在 `mqtt_ot_bridge_start()` 中,`esp_mqtt_client_start` 之前加入:
```c
    ESP_RETURN_ON_ERROR(coap_ensure_started(), TAG, "coap start failed");
```
并在顶部包含 `#include "esp_check.h"`。

- [ ] **Step 4: 编译验证**

Run:
```bash
source /d/esp/v6.0.2/esp-idf/export.sh
cd basic_thread_border_router && idf.py build 2>&1 | tail -20
```
Expected: 编译成功。

- [ ] **Step 5: 设备端 monitor 验证(需真实硬件或 RCP)**

Run: `idf.py -p <PORT> flash monitor`
Expected 日志:启动后出现 `coap start err=0`;MQTT 连上后 `MQTT connected` 与 `subscribed under prefix 'otbr'`。用外部工具向 BR 的 `/ack` 发一条 CoAP → 日志出现 `/ack got N bytes -> uplink`,且 broker 上 `otbr/dev/response` 收到该 payload。
无硬件时:仅确认编译通过,monitor 步骤标记为"待硬件验证"。

- [ ] **Step 6: 提交**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c
git commit -m "feat(mqtt_ot_bridge): start CoAP, register /ack resource, forward uplink to MQTT"
```

---

## Task 7: 单播 CON 与组播 NON 下行(编译 + monitor 验证)

**Files:**
- Modify: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`

**Interfaces:**
- Consumes: `bridge_cmd_t`(Task 2)、`coap_read_payload`(Task 6)、`s_coap_started`、SRP 选路 `bridge_lookup_ipv6_by_eui64`(Task 8;本任务先声明 `extern`/前置 static 原型,Task 8 实现)。
- Produces: `static void coap_send(const otIp6Address *dst, bool confirmable, const char *payload, int len);`、`static void unicast_response_handler(void *ctx, otMessage *msg, const otMessageInfo *info, otError result);`。

**说明:** 单播选路依赖 SRP(Task 8)。为保持任务可独立编译,本任务先声明原型
`bool bridge_lookup_ipv6_by_eui64(const uint8_t eui64[8], otIp6Address *out);` 并在文件内提供一个 **临时 weak 兜底**(始终返回 false + 警告),Task 8 用真实实现替换。这样 Task 7 可独立编译、monitor 观察组播路径,单播路径在 Task 8 完成后打通。

- [ ] **Step 1: 声明 SRP 选路原型 + 临时兜底**

在 `handle_downlink` 之前加入:
```c
// Task 8 提供真实实现(遍历 SRP 表)。此处 weak 兜底保证 Task 7 可独立编译。
bool bridge_lookup_ipv6_by_eui64(const uint8_t eui64[8], otIp6Address *out) __attribute__((weak));
bool bridge_lookup_ipv6_by_eui64(const uint8_t eui64[8], otIp6Address *out) {
    (void)eui64; (void)out;
    ESP_LOGW(TAG, "SRP lookup not implemented yet (Task 8)");
    return false;
}
```

- [ ] **Step 2: 实现 CoAP 发送与单播响应回调**

在 `handle_downlink` 之前加入:
```c
static void unicast_response_handler(void *ctx, otMessage *msg, const otMessageInfo *info, otError result) {
    (void)ctx; (void)info;
    if (result != OT_ERROR_NONE || msg == NULL) {
        ESP_LOGW(TAG, "unicast no response: err=%d", result);
        return;
    }
    char payload[512];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    publish_uplink(payload, len);
}

// confirmable=true → 单播 CON(带响应回调);false → 组播 NON(无回调)。
// 调用者必须已持有 OT 锁。
static void coap_send(const otIp6Address *dst, bool confirmable, const char *payload, int len) {
    otInstance *inst = esp_openthread_get_instance();
    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) {
        ESP_LOGE(TAG, "coap new message failed");
        return;
    }
    otCoapMessageInit(msg, confirmable ? OT_COAP_TYPE_CONFIRMABLE : OT_COAP_TYPE_NON_CONFIRMABLE,
                      OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_MQTT_OT_BRIDGE_COAP_URI);
    if (len > 0) {
        otCoapMessageSetPayloadMarker(msg);
        otMessageAppend(msg, payload, (uint16_t)len);
    }
    otMessageInfo info;
    memset(&info, 0, sizeof(info));
    info.mPeerAddr = *dst;
    info.mPeerPort = CONFIG_MQTT_OT_BRIDGE_COAP_PORT;

    otError err;
    if (confirmable) {
        err = otCoapSendRequest(inst, msg, &info, unicast_response_handler, NULL);
    } else {
        err = otCoapSendRequest(inst, msg, &info, NULL, NULL);
    }
    if (err != OT_ERROR_NONE) {
        otMessageFree(msg);   // 发送失败需释放
        ESP_LOGE(TAG, "coap send err=%d", err);
    }
}
```
顶部包含区补充 `#include <string.h>`(已在)与确保 `openthread/coap.h` 提供 `OT_COAP_CODE_POST`(已在)。

- [ ] **Step 3: 把 handle_downlink 接到 CoAP**

用真实逻辑替换 Task 5 的桩 `handle_downlink`:
```c
static void handle_downlink(const char *topic_suffix, const char *data, int data_len) {
    bridge_cmd_t cmd;
    if (!bridge_topic_parse(topic_suffix, &cmd)) {
        ESP_LOGW(TAG, "unknown downlink topic suffix: %s", topic_suffix);
        return;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    if (cmd.kind == BRIDGE_CMD_UNICAST) {
        otIp6Address dst;
        if (bridge_lookup_ipv6_by_eui64(cmd.eui64, &dst)) {
            coap_send(&dst, true, data, data_len);
        } else {
            ESP_LOGW(TAG, "unicast target not found in SRP");
        }
    } else if (cmd.kind == BRIDGE_CMD_MULTICAST) {
        otIp6Address maddr;
        if (otIp6AddressFromString(CONFIG_MQTT_OT_BRIDGE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
            coap_send(&maddr, false, data, data_len);   // 组播必须 NON
        } else {
            ESP_LOGE(TAG, "bad multicast addr");
        }
    }
    esp_openthread_lock_release();
}
```

- [ ] **Step 4: 编译验证**

Run: `source /d/esp/v6.0.2/esp-idf/export.sh && cd basic_thread_border_router && idf.py build 2>&1 | tail -20`
Expected: 编译成功。

- [ ] **Step 5: monitor 验证(需硬件 + 至少一台 H2 设备)**

Run: `idf.py -p <PORT> flash monitor`,并向 broker publish:
- `otbr/cmd/multicast` payload `{"reqid":"t1","cmd":"on"}` → BR 日志出现组播发送(无 `coap send err`),设备端收到并可经 `/ack` 回流,broker `otbr/dev/response` 出现响应。
- `otbr/cmd/unicast/<eui64>`(Task 8 完成后)→ 命中 SRP,单播 CON 得到响应并发布到 `dev/response`。
Expected:组播路径此任务即可通;单播路径在 Task 8 后打通。无硬件时仅确认编译。

- [ ] **Step 6: 提交**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c
git commit -m "feat(mqtt_ot_bridge): unicast CON + multicast NON downlink over CoAP"
```

---

## Task 8: SRP 设备发现、EUI64→IPv6 选路、清单周期上报(编译 + monitor 验证)

**Files:**
- Modify: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`

**Interfaces:**
- Consumes: `bridge_registry_to_json`/`bridge_dev_entry_t`(Task 3)、`eui64_to_string`(Task 1)、`publish_uplink` 同款 publish 封装、`otSrpServerGetNextHost`/`otSrpServerHostGetFullName`/`otSrpServerHostGetAddresses`/`otSrpServerHostGetNextService`/`otSrpServerServiceGetInstanceName`。
- Produces: 真实 `bridge_lookup_ipv6_by_eui64`(替换 Task 7 的 weak 兜底)、`static void registry_collect(bridge_dev_entry_t *arr, size_t cap, size_t *out_count);`、`static void registry_timer_cb(TimerHandle_t t);`。

**约定:** 设备的 SRP **host full name** 以 EUI64 开头(如 `1a2b3c4d5e6f7080.default.service.arpa.` 或 host 名即 `1a2b3c4d5e6f7080`)。`bridge_lookup_ipv6_by_eui64` 通过比对 host 名前 16 个字符对应的 EUI64 来匹配;`registry_collect` 同样从 host 名提取 EUI64。此约定写入 Task 9 的设备端固件契约文档。

- [ ] **Step 1: 增加 SRP 头 + 定时器头**

顶部包含区追加:
```c
#include "openthread/srp_server.h"
#include "freertos/timers.h"
#include "bridge_registry_json.h"
#include "bridge_eui64.h"
```

- [ ] **Step 2: 实现从 host 名提取 EUI64 的辅助 + 遍历收集**

在文件中(`coap_send` 附近)加入:
```c
// 从 SRP host full name 前缀提取 EUI64 文本(取前16个十六进制字符)。成功写入 out[17]。
static bool host_name_to_eui64_str(const char *host_name, char out[17]) {
    if (host_name == NULL) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        char c = host_name[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
        out[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    out[16] = '\0';
    return true;
}

// 遍历 SRP 表收集设备。调用者必须已持 OT 锁。
static void registry_collect(bridge_dev_entry_t *arr, size_t cap, size_t *out_count) {
    otInstance *inst = esp_openthread_get_instance();
    size_t n = 0;
    const otSrpServerHost *host = NULL;
    while ((host = otSrpServerGetNextHost(inst, host)) != NULL && n < cap) {
        if (otSrpServerHostIsDeleted(host)) {
            continue;
        }
        const char *hname = otSrpServerHostGetFullName(host);
        char eui[17];
        if (!host_name_to_eui64_str(hname, eui)) {
            continue;   // 不符合 EUI64 命名约定的 host 跳过
        }
        uint8_t naddr = 0;
        const otIp6Address *addrs = otSrpServerHostGetAddresses(host, &naddr);
        bridge_dev_entry_t *e = &arr[n];
        memcpy(e->eui64, eui, sizeof(eui));
        e->ipv6[0] = '\0';
        if (naddr > 0) {
            otIp6AddressToString(&addrs[0], e->ipv6, sizeof(e->ipv6));
        }
        e->service[0] = '\0';
        const otSrpServerService *svc = otSrpServerHostGetNextService(host, NULL);
        if (svc != NULL) {
            const char *iname = otSrpServerServiceGetInstanceName(svc);
            if (iname != NULL) {
                strncpy(e->service, iname, sizeof(e->service) - 1);
                e->service[sizeof(e->service) - 1] = '\0';
            }
        }
        n++;
    }
    *out_count = n;
}
```
注:`otIp6AddressToString(const otIp6Address*, char*, uint16_t)` 在 `ip6.h`;确认包含。

- [ ] **Step 3: 用真实 SRP 查询替换 weak 兜底**

在文件中新增(非 weak,链接器优先取强符号覆盖 Task 7 的 weak):
```c
bool bridge_lookup_ipv6_by_eui64(const uint8_t eui64[8], otIp6Address *out) {
    char want[17];
    eui64_to_string(eui64, want);
    otInstance *inst = esp_openthread_get_instance();
    const otSrpServerHost *host = NULL;
    while ((host = otSrpServerGetNextHost(inst, host)) != NULL) {
        if (otSrpServerHostIsDeleted(host)) {
            continue;
        }
        char eui[17];
        if (!host_name_to_eui64_str(otSrpServerHostGetFullName(host), eui)) {
            continue;
        }
        if (strcmp(eui, want) == 0) {
            uint8_t naddr = 0;
            const otIp6Address *addrs = otSrpServerHostGetAddresses(host, &naddr);
            if (naddr > 0) {
                *out = addrs[0];
                return true;
            }
        }
    }
    return false;
}
```
调用方(`handle_downlink`)已持锁,故此函数不再自行 acquire。

- [ ] **Step 4: 清单定时器 + 发布**

加入定时器回调与启动:
```c
static TimerHandle_t s_registry_timer = NULL;

static void publish_registry(void) {
    bridge_dev_entry_t entries[32];
    size_t count = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    registry_collect(entries, 32, &count);
    esp_openthread_lock_release();

    char *json = bridge_registry_to_json(entries, count);
    if (json == NULL) {
        return;
    }
    if (s_client != NULL) {
        char t[128];
        snprintf(t, sizeof(t), "%s/dev/registry", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
        esp_mqtt_client_publish(s_client, t, json, 0, 0, /*retain=*/1);
        ESP_LOGI(TAG, "published registry: %d devices", (int)count);
    }
    free(json);
}

static void registry_timer_cb(TimerHandle_t t) {
    (void)t;
    publish_registry();
}
```
在 `mqtt_ot_bridge_start()` 内(`esp_mqtt_client_start` 之后)启动定时器:
```c
    s_registry_timer = xTimerCreate("otbr_reg", pdMS_TO_TICKS(CONFIG_MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S * 1000),
                                    pdTRUE, NULL, registry_timer_cb);
    if (s_registry_timer != NULL) {
        xTimerStart(s_registry_timer, 0);
    }
```
在 `mqtt_ot_bridge_stop()` 内删除定时器:
```c
    if (s_registry_timer != NULL) {
        xTimerStop(s_registry_timer, 0);
        xTimerDelete(s_registry_timer, 0);
        s_registry_timer = NULL;
    }
```

- [ ] **Step 5: 编译验证**

Run: `source /d/esp/v6.0.2/esp-idf/export.sh && cd basic_thread_border_router && idf.py build 2>&1 | tail -20`
Expected: 编译成功;链接期无 `multiple definition of bridge_lookup_ipv6_by_eui64`(强符号覆盖 weak;若报重复定义,删除 Task 7 的 weak 兜底改为仅原型声明)。

- [ ] **Step 6: monitor 验证(需硬件 + H2 设备跑 SRP 客户端)**

Run: `idf.py -p <PORT> flash monitor`
Expected:H2 设备完成 SRP 注册后,周期日志 `published registry: N devices`,broker `otbr/dev/registry`(retained)出现 `[{"eui64":...,"ipv6":...,"service":...}]`;此后向 `otbr/cmd/unicast/<eui64>` 发命令,单播 CON 打通并在 `dev/response` 得到响应。

- [ ] **Step 7: 提交**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c
git commit -m "feat(mqtt_ot_bridge): SRP device discovery, EUI64->IPv6 routing, periodic registry publish"
```

---

## Task 9: SRP server 前置确认 + 文档(编译验证)

**Files:**
- Create: `common/mqtt_ot_bridge/README.md`

**Interfaces:**
- Consumes: 全部前序任务的行为约定。
- Produces: 面向使用者的配置说明 + 设备端固件契约(SRP 命名、CoAP 资源、组播抖动)。

- [ ] **Step 1: 确认 SRP server 默认启用(无需改 sdkconfig)**

**纠正(实现期核实):** IDF v6.0.2 的 Kconfig **没有** `OPENTHREAD_SRP_SERVER` 开关。SRP server 是 OpenThread 编译期特性,由 `openthread-core-esp32x-ftd-config.h:552` 将 `OPENTHREAD_CONFIG_SRP_SERVER_ENABLE` 默认设为 `1`,故 ESP32 FTD/BR 固件默认已启用,`otSrpServer*` API 可直接使用。**不要**向 `sdkconfig.defaults` 添加 `CONFIG_OPENTHREAD_SRP_SERVER=y`(该符号不存在,会产生未知符号告警)。此步仅为记录,无文件改动。

- [ ] **Step 2: 写 README(设备端契约)**

`common/mqtt_ot_bridge/README.md`:
```markdown
# mqtt_ot_bridge

BR 侧的无状态 MQTT↔CoAP 桥接组件。

## BR 配置(menuconfig → MQTT OT Bridge)
- `MQTT_OT_BRIDGE_ENABLE`：启用桥接
- `MQTT_OT_BRIDGE_BROKER_URI`：`mqtt://host:port`(局域网，无 TLS)
- `MQTT_OT_BRIDGE_USERNAME` / `_PASSWORD`：登陆凭据
- `MQTT_OT_BRIDGE_TOPIC_PREFIX`：topic 前缀(默认 `otbr`)
- `MQTT_OT_BRIDGE_MULTICAST_ADDR`：组播地址(默认 `ff03::1`)
- `MQTT_OT_BRIDGE_COAP_URI`：设备下行资源(默认 `ctrl`)
- `MQTT_OT_BRIDGE_ACK_URI`：BR 上行资源(默认 `ack`)
- `MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S`：清单发布周期

前置：`CONFIG_OPENTHREAD_SRP_SERVER=y`。

## MQTT Topic 约定
| 方向 | Topic | 说明 |
|------|-------|------|
| 下行单播 | `<prefix>/cmd/unicast/<eui64>` | payload 透传进 CoAP CON |
| 下行组播 | `<prefix>/cmd/multicast` | payload 透传进 CoAP NON → 组播地址 |
| 上行响应 | `<prefix>/dev/response` | 设备响应 payload 原样发布 |
| 设备清单 | `<prefix>/dev/registry` | retained，`[{eui64,ipv6,service}]` |

BR 不解析 payload：reqid/语义由服务端定义，服务端凭 reqid 自行对账。

## 设备端(ESP32-H2)固件契约
1. **SRP 注册**：host full name 以 16 位小写十六进制 EUI64 开头(如 `1a2b3c4d5e6f7080`)；注册可路由 IPv6 地址与服务实例名。
2. **CoAP server**：监听 `MQTT_OT_BRIDGE_COAP_URI`(默认 `ctrl`)接收控制。
3. **组播**：加入 `MQTT_OT_BRIDGE_MULTICAST_ADDR`(默认 `ff03::1`)。
4. **上报**：执行后单播 CoAP 到 BR 的 `MQTT_OT_BRIDGE_ACK_URI`(默认 `ack`)，payload 内带服务端下发的 reqid。
5. **组播抖动**：收到组播命令后随机延迟 0~500ms 再上报，避免响应风暴。
```

- [ ] **Step 3: 提交**

```bash
git add basic_thread_border_router/sdkconfig.defaults common/mqtt_ot_bridge/README.md
git commit -m "docs(mqtt_ot_bridge): enable SRP server + document topics and device firmware contract"
```

---

## Self-Review Notes

- **Spec 覆盖**:mqtt-bridge(连接登陆=T5、订阅路由=T5/T7、上行发布=T6、透传=T5~T7 全程不解析 payload);coap-device-control(单播CON=T7、组播NON=T7、/ack资源=T6、无状态=贯穿);device-registry(SRP发现=T8、清单上报=T8、EUI64→IPv6选路=T8)。全部有对应任务。
- **占位符扫描**:无 TBD/TODO;每个代码步骤含完整代码。硬件相关步骤给出明确 monitor 预期日志。
- **类型一致性**:`bridge_cmd_t`/`bridge_cmd_kind_t`(T2)、`bridge_dev_entry_t`(T3)、`bridge_lookup_ipv6_by_eui64` 签名(T7 声明↔T8 实现一致)、`publish_uplink`/`coap_read_payload`/`s_client`/`s_coap_started` 跨任务名称统一。
- **已知风险**:`espressif/mqtt` 托管拉取需联网(T5 Step5 验证);libcjson 主机测试可选(T3 备注);SRP host 命名约定是 BR↔设备的硬契约(T8/T9 文档化);`otMessageFree`/`otIp6AddressToString` 已确认在 `message.h`/`ip6.h`。
