# IoT Device Components Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor device-side IoT firmware into reusable `common/` components — a protocol+transport core plus one-event-one-component pluggable capabilities — and migrate the BR to the unified `{reqid,event,data}` protocol.

**Architecture:** A new `iot_device_core` component owns SRP registration, CoAP transport, the `{reqid,event,data}` envelope, an `event → handler` dispatch table, and a dedicated command **worker task** (CoAP callback only enqueues; the worker runs handlers so they may block and take the OT lock themselves). Three sample capability components (`iot_cap_switch`, `iot_cap_pwm_set`, `iot_cap_adc_read`) each register one event via an explicit `iot_cap_xxx_init()` call. `ot_iot_device` becomes a thin demo that wires the core + 3 capabilities. The BR (`mqtt_ot_bridge`) renames its uplink topic to `cmd/resp`, subscribes a dedicated `cmd/registry` topic, and answers `registry_list` from its SRP table while all control downlinks stay byte-passthrough.

**Tech Stack:** ESP-IDF v6.0.2, OpenThread (CoAP, SRP client/server), esp-mqtt (`espressif/mqtt`), cJSON (`espressif/cjson`), FreeRTOS (queues, timers), ESP-IDF driver layer (gpio/ledc/adc). Host unit tests in C via gcc + `assert` (pattern: `common/mqtt_ot_bridge/test/host/`).

## Global Constraints

- ESP-IDF version floor: `idf >= 5.1.0` in every component's `idf_component.yml` (matches existing components). Actual toolchain in use: **v6.0.2**.
- IDF v6 managed component naming: dependency key `espressif/cjson`; CMake `REQUIRES`/`PRIV_REQUIRES` token `espressif__cjson`; header `cJSON.h`. (esp-mqtt: `espressif/mqtt` / `espressif__mqtt` / `mqtt_client.h` — BR only.)
- `main` component of an example project MUST NOT declare `REQUIRES`/`PRIV_REQUIRES` (MINIMAL_BUILD + main sees all components implicitly; listing them switches to strict header mode and breaks implicit headers like `nvs_flash.h`).
- `common/` components are referenced by an example via `main/idf_component.yml` path deps, e.g. `iot_device_core: { path: ../../common/iot_device_core }`.
- EUI64 wire format: 16 lowercase hex chars, no separators. SRP host full name starts with this EUI64.
- CoAP: unicast AND multicast both use **NON** (never CON) — device replies flow back via BR `/ack`, so CON would retransmit and cause duplicate execution. Multicast target `ff03::1`.
- Never `vTaskDelay` or run blocking work inside a CoAP callback or FreeRTOS timer-service task (small stacks; blocks the OT task). Large buffers → heap, not stack, in timer/callback contexts.
- Global status codes: `0` success / `-1` param error / `-2` busy/fail / `-3` unsupported event / `-4` hardware error. `msg` is `"success"` when code==0 else `"fail"`.
- Response event name = downlink event name + `"_resp"` suffix. Response reqid == downlink reqid. Response root MUST carry `eui64`.
- Out of scope this change: periodic reporting framework, `report_freq_set/get`, `sensor_report`, `dev/up` topic, NVS persistence. Do not build them.
- Build/flash only in ESP-IDF PowerShell/CMD, never Git Bash. Host unit tests run anywhere with gcc + libcjson.

---

## File Structure

**New component `common/iot_device_core/`:**
- `CMakeLists.txt` — registers `src/`, `include/`, REQUIRES `openthread`, PRIV_REQUIRES `espressif__cjson` `freertos`.
- `idf_component.yml` — deps `espressif/cjson`, `idf >= 5.1.0`.
- `Kconfig.projbuild` — service name, ctrl/ack URIs, multicast addr, CoAP port, worker stack/queue sizes.
- `include/iot_device_core.h` — public API: `iot_device_core_start()`, `iot_device_register_handler()`, `iot_event_handler_t` typedef, status-code macros.
- `src/iot_envelope.c` + `src/iot_envelope.h` — pure JSON envelope parse/build (host-testable).
- `src/iot_dispatch.c` + `src/iot_dispatch.h` — pure `event→handler` table (host-testable).
- `src/iot_device_eui64.c` + `.h` — EUI64 8-byte → 17-char lowercase (copied pattern).
- `src/iot_device_core.c` — SRP register, CoAP server, worker task, reporting glue (device-only, not host-tested).
- `test/host/Makefile` + `test/host/test_envelope.c` + `test/host/test_dispatch.c` — host unit tests.

**New capability components** (each: `CMakeLists.txt`, `idf_component.yml`, `include/iot_cap_*.h`, `src/iot_cap_*.c`):
- `common/iot_cap_switch/` — `iot_cap_switch_init()`, registers `switch`.
- `common/iot_cap_pwm_set/` — `iot_cap_pwm_set_init()`, registers `pwm_set`, LEDC channel-alloc static state.
- `common/iot_cap_adc_read/` — `iot_cap_adc_read_init()`, registers `adc_read`, ADC cal handle static state.

**Modified — `ot_iot_device/`:**
- `main/esp_ot_iot_device.c:80` — replace `iot_device_start()` with core + capability init calls.
- `main/CMakeLists.txt` — drop `iot_device.c`/`device_switch.c`/`device_eui64.c`/`iot_device.h`; keep only `esp_ot_iot_device.c`.
- `main/idf_component.yml` — add path deps to the 4 new components.
- Delete: `main/iot_device.c`, `main/iot_device.h`, `main/device_switch.c`, `main/device_switch.h`, `main/device_eui64.c`, `main/device_eui64.h`.

**Modified — `common/mqtt_ot_bridge/`:**
- `src/bridge_registry_json.c/.h` — add `bridge_registry_list_resp_to_json(reqid, br_eui64, entries, count)` (host-testable).
- `src/mqtt_ot_bridge.c` — uplink topic `dev/response`→`cmd/resp`; subscribe `cmd/registry`; `registry_list` handler; BR's own EUI64.
- `Kconfig.projbuild` — no new keys required (topics derived from prefix); optionally document `cmd/registry`.
- `test/host/Makefile` + `test/host/test_registry_json.c` — extend for the new response builder.

---

## Task 1: Core scaffold — component skeleton that builds empty

**Files:**
- Create: `common/iot_device_core/CMakeLists.txt`
- Create: `common/iot_device_core/idf_component.yml`
- Create: `common/iot_device_core/Kconfig.projbuild`
- Create: `common/iot_device_core/include/iot_device_core.h`
- Create: `common/iot_device_core/src/iot_device_core.c`

**Interfaces:**
- Produces: public header `iot_device_core.h` with status macros, `iot_event_handler_t`, `iot_device_core_start(void)`, `iot_device_register_handler(const char *event, iot_event_handler_t h)`. Bodies are stubs filled by later tasks.

- [ ] **Step 1: Write the public header**

Create `common/iot_device_core/include/iot_device_core.h`:

```c
#pragma once
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// 全局状态码(协议规范 3.3)
#define IOT_CODE_OK          0    // 成功
#define IOT_CODE_PARAM      -1    // 参数错误
#define IOT_CODE_BUSY       -2    // 忙/执行失败
#define IOT_CODE_UNSUPPORTED -3   // 不支持该事件
#define IOT_CODE_HW         -4    // 硬件异常

// 能力 handler：读 data、干活、填 resp_data、返状态码。
// 运行于内核 worker 任务上下文，允许阻塞；访问 OT API 时须自行持 OT 锁。
typedef int (*iot_event_handler_t)(const cJSON *data, cJSON *resp_data);

// 启动内核：SRP 自动注册 + CoAP server + 命令 worker 任务。
// 必须在 esp_openthread_start() 之后调用。
void iot_device_core_start(void);

// 注册一个 event 的 handler。event 字符串须在程序生命周期内保持有效
// (通常是字面量)。返回 true 成功，表满或重复返回 false。
bool iot_device_register_handler(const char *event, iot_event_handler_t handler);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write stub source**

Create `common/iot_device_core/src/iot_device_core.c`:

```c
#include "iot_device_core.h"

void iot_device_core_start(void) {
    // 由后续任务实现
}

bool iot_device_register_handler(const char *event, iot_event_handler_t handler) {
    (void)event; (void)handler;
    return false;  // 由 Task 3 实现
}
```

- [ ] **Step 3: Write CMakeLists.txt**

Create `common/iot_device_core/CMakeLists.txt`:

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES openthread
                       PRIV_REQUIRES espressif__cjson freertos)
```

- [ ] **Step 4: Write idf_component.yml**

Create `common/iot_device_core/idf_component.yml`:

```yaml
dependencies:
  espressif/cjson: "^1.7.19"
  idf:
    version: ">=5.1.0"
```

- [ ] **Step 5: Write Kconfig.projbuild**

Create `common/iot_device_core/Kconfig.projbuild`:

```
menu "IoT Device Core"

    config IOT_CORE_SERVICE_NAME
        string "SRP service name"
        default "_iot._udp"

    config IOT_CORE_CTRL_URI
        string "CoAP control resource path"
        default "ctrl"

    config IOT_CORE_ACK_URI
        string "BR uplink resource path"
        default "ack"

    config IOT_CORE_MULTICAST_ADDR
        string "Multicast group to join"
        default "ff03::1"

    config IOT_CORE_COAP_PORT
        int "CoAP port"
        default 5683

    config IOT_CORE_WORKER_STACK
        int "Command worker task stack size (bytes)"
        default 4096

    config IOT_CORE_WORKER_QUEUE_LEN
        int "Command worker queue length"
        default 8

endmenu
```

- [ ] **Step 6: Verify it compiles in an existing example (sanity)**

The component has no consumer yet, so build is deferred to Task 8. For now verify the files parse by compiling the stub on host:

Run: `gcc -fsyntax-only -Icommon/iot_device_core/include -I$(pkg-config --variable=includedir libcjson)/cjson common/iot_device_core/src/iot_device_core.c`
Expected: no output (exit 0). If `cjson` include path differs, use `pkg-config --cflags libcjson`.

- [ ] **Step 7: Commit**

```bash
git add common/iot_device_core/
git commit -m "feat(iot_device_core): scaffold component with public API stubs"
```

---

## Task 2: Envelope parse/build (pure, host-TDD)

**Files:**
- Create: `common/iot_device_core/src/iot_envelope.h`
- Create: `common/iot_device_core/src/iot_envelope.c`
- Create: `common/iot_device_core/test/host/Makefile`
- Test: `common/iot_device_core/test/host/test_envelope.c`

**Interfaces:**
- Produces:
  - `bool iot_envelope_parse(const char *json, char *reqid, size_t reqid_cap, char *event, size_t event_cap, cJSON **data_out)` — parses `{reqid,event,data}`. On success `*data_out` is a **detached deep copy** the caller must `cJSON_Delete`. Returns false if `event` missing/not-string. `reqid`/`data` optional (reqid→"" , data→empty object) .
  - `char *iot_envelope_build_resp(const char *reqid, const char *eui64, const char *event, int code, const cJSON *resp_data)` — builds `{reqid,eui64,event:"<event>_resp",code,msg,data}`; `msg` = code==0?"success":"fail"; `data` = deep copy of `resp_data` or `{}` if NULL. Returns malloc'd string (caller frees) or NULL.
- Consumes: cJSON.

- [ ] **Step 1: Write the failing tests**

Create `common/iot_device_core/test/host/test_envelope.c`:

```c
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "../../src/iot_envelope.h"

static void test_parse_ok(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r1\",\"event\":\"switch\",\"data\":{\"gpio\":2,\"action\":\"on\"}}";
    assert(iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(strcmp(reqid, "r1") == 0);
    assert(strcmp(event, "switch") == 0);
    assert(data != NULL);
    cJSON *gpio = cJSON_GetObjectItem(data, "gpio");
    assert(cJSON_IsNumber(gpio) && gpio->valueint == 2);
    cJSON_Delete(data);
}

static void test_parse_empty_data(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r2\",\"event\":\"registry_list\",\"data\":{}}";
    assert(iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(strcmp(event, "registry_list") == 0);
    assert(data != NULL && cJSON_IsObject(data));
    cJSON_Delete(data);
}

static void test_parse_missing_event(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r3\",\"data\":{}}";
    assert(!iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(data == NULL);
}

static void test_parse_bad_json(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    assert(!iot_envelope_parse("not json", reqid, sizeof(reqid), event, sizeof(event), &data));
}

static void test_build_success(void) {
    cJSON *rd = cJSON_CreateObject();
    cJSON_AddNumberToObject(rd, "gpio", 2);
    cJSON_AddStringToObject(rd, "status", "on");
    char *out = iot_envelope_build_resp("r1", "1a2b3c4d5e6f7080", "switch", 0, rd);
    cJSON_Delete(rd);
    assert(out != NULL);
    cJSON *p = cJSON_Parse(out);
    assert(strcmp(cJSON_GetObjectItem(p, "reqid")->valuestring, "r1") == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "eui64")->valuestring, "1a2b3c4d5e6f7080") == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "event")->valuestring, "switch_resp") == 0);
    assert(cJSON_GetObjectItem(p, "code")->valueint == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "msg")->valuestring, "success") == 0);
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(p, "data"), "gpio")->valueint == 2);
    cJSON_Delete(p);
    free(out);
}

static void test_build_fail_null_data(void) {
    char *out = iot_envelope_build_resp("r9", "aabb", "adc_read", -1, NULL);
    assert(out != NULL);
    cJSON *p = cJSON_Parse(out);
    assert(cJSON_GetObjectItem(p, "code")->valueint == -1);
    assert(strcmp(cJSON_GetObjectItem(p, "msg")->valuestring, "fail") == 0);
    assert(cJSON_IsObject(cJSON_GetObjectItem(p, "data")));  // 空对象
    cJSON_Delete(p);
    free(out);
}

int main(void) {
    test_parse_ok();
    test_parse_empty_data();
    test_parse_missing_event();
    test_parse_bad_json();
    test_build_success();
    test_build_fail_null_data();
    printf("test_envelope OK\n");
    return 0;
}
```

- [ ] **Step 2: Write the host Makefile**

Create `common/iot_device_core/test/host/Makefile`:

```make
CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g
SRC = ../../src
CJSON_CFLAGS = $(shell pkg-config --cflags libcjson 2>/dev/null)
CJSON_LIBS = $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)

.PHONY: all clean
all: test_envelope test_dispatch
	./test_envelope
	./test_dispatch

test_envelope: test_envelope.c $(SRC)/iot_envelope.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) -o $@ $^ $(CJSON_LIBS)

test_dispatch: test_dispatch.c $(SRC)/iot_dispatch.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) -o $@ $^ $(CJSON_LIBS)

clean:
	rm -f test_envelope test_dispatch
```

Note: `test_dispatch` target is added now but its source arrives in Task 3. Build only the envelope target this task.

- [ ] **Step 3: Run test to verify it fails**

Run: `cd common/iot_device_core/test/host && make test_envelope`
Expected: FAIL — linker/compiler error, `iot_envelope.c: No such file` or undefined `iot_envelope_parse`.

- [ ] **Step 4: Write the header**

Create `common/iot_device_core/src/iot_envelope.h`:

```c
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

// 解析下行 {reqid,event,data}。event 必填(缺失/非字符串→false)。
// reqid 缺失→""; data 缺失→空对象。成功时 *data_out 为独立深拷贝，调用者负责 cJSON_Delete。
bool iot_envelope_parse(const char *json,
                        char *reqid, size_t reqid_cap,
                        char *event, size_t event_cap,
                        cJSON **data_out);

// 构造上行响应 {reqid,eui64,event:"<event>_resp",code,msg,data}。
// msg = code==0?"success":"fail"; data = resp_data 深拷贝或空对象。
// 返回 malloc 字符串(调用者 free)或 NULL。
char *iot_envelope_build_resp(const char *reqid, const char *eui64,
                              const char *event, int code,
                              const cJSON *resp_data);
```

- [ ] **Step 5: Write the implementation**

Create `common/iot_device_core/src/iot_envelope.c`:

```c
#include "iot_envelope.h"
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool iot_envelope_parse(const char *json,
                        char *reqid, size_t reqid_cap,
                        char *event, size_t event_cap,
                        cJSON **data_out) {
    if (data_out) *data_out = NULL;
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    cJSON *jevent = cJSON_GetObjectItem(root, "event");
    if (!cJSON_IsString(jevent) || jevent->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }
    copy_str(event, event_cap, jevent->valuestring);

    cJSON *jreqid = cJSON_GetObjectItem(root, "reqid");
    copy_str(reqid, reqid_cap, cJSON_IsString(jreqid) ? jreqid->valuestring : "");

    if (data_out) {
        cJSON *jdata = cJSON_GetObjectItem(root, "data");
        if (jdata != NULL) {
            *data_out = cJSON_Duplicate(jdata, true);
        } else {
            *data_out = cJSON_CreateObject();
        }
    }
    cJSON_Delete(root);
    return true;
}

char *iot_envelope_build_resp(const char *reqid, const char *eui64,
                              const char *event, int code,
                              const cJSON *resp_data) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "reqid", reqid ? reqid : "");
    cJSON_AddStringToObject(root, "eui64", eui64 ? eui64 : "");

    char ev[80];
    snprintf(ev, sizeof(ev), "%s_resp", event ? event : "");
    cJSON_AddStringToObject(root, "event", ev);

    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", code == 0 ? "success" : "fail");

    cJSON *data = resp_data ? cJSON_Duplicate(resp_data, true) : cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
```

Add `#include <stdio.h>` at top for `snprintf`.

- [ ] **Step 6: Run test to verify it passes**

Run: `cd common/iot_device_core/test/host && make test_envelope`
Expected: PASS — prints `test_envelope OK`.

- [ ] **Step 7: Commit**

```bash
git add common/iot_device_core/src/iot_envelope.* common/iot_device_core/test/host/Makefile common/iot_device_core/test/host/test_envelope.c
git commit -m "feat(iot_device_core): add JSON envelope parse/build with host tests"
```

---

## Task 3: Dispatch table (pure, host-TDD)

**Files:**
- Create: `common/iot_device_core/src/iot_dispatch.h`
- Create: `common/iot_device_core/src/iot_dispatch.c`
- Test: `common/iot_device_core/test/host/test_dispatch.c`

**Interfaces:**
- Consumes: `iot_event_handler_t` from `iot_device_core.h`.
- Produces:
  - `bool iot_dispatch_register(const char *event, iot_event_handler_t h)` — adds to a fixed table (cap 16). Duplicate event or full table → false.
  - `iot_event_handler_t iot_dispatch_lookup(const char *event)` — returns handler or NULL.
  - `void iot_dispatch_reset(void)` — clears table (test helper).

- [ ] **Step 1: Write the failing test**

Create `common/iot_device_core/test/host/test_dispatch.c`:

```c
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "cJSON.h"
#include "../../src/iot_dispatch.h"

static int h_switch(const cJSON *d, cJSON *r) { (void)d; (void)r; return 0; }
static int h_pwm(const cJSON *d, cJSON *r) { (void)d; (void)r; return 0; }

int main(void) {
    iot_dispatch_reset();

    assert(iot_dispatch_register("switch", h_switch));
    assert(iot_dispatch_register("pwm_set", h_pwm));
    // 重复注册被拒
    assert(!iot_dispatch_register("switch", h_switch));

    assert(iot_dispatch_lookup("switch") == h_switch);
    assert(iot_dispatch_lookup("pwm_set") == h_pwm);
    // 未注册返回 NULL
    assert(iot_dispatch_lookup("adc_read") == NULL);

    printf("test_dispatch OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd common/iot_device_core/test/host && make test_dispatch`
Expected: FAIL — `iot_dispatch.c` / undefined symbols.

Note: `test_dispatch.c` includes `cJSON.h` only for the handler signature; the Makefile already links libcjson.

- [ ] **Step 3: Write the header**

Create `common/iot_device_core/src/iot_dispatch.h`:

```c
#pragma once
#include <stdbool.h>
#include "iot_device_core.h"   // iot_event_handler_t

#define IOT_DISPATCH_MAX 16

// 注册 event→handler。重复或表满返回 false。event 指针须长期有效。
bool iot_dispatch_register(const char *event, iot_event_handler_t h);

// 查表。未命中返回 NULL。
iot_event_handler_t iot_dispatch_lookup(const char *event);

// 清空表(测试辅助)。
void iot_dispatch_reset(void);
```

For host compilation, `iot_device_core.h` includes `cJSON.h` — the Makefile's `CJSON_CFLAGS` covers the include path. Add `-I../../include` to the `test_dispatch` compile so the header resolves. Update Makefile target:

```make
test_dispatch: test_dispatch.c $(SRC)/iot_dispatch.c
	$(CC) $(CFLAGS) $(CJSON_CFLAGS) -I../../include -o $@ $^ $(CJSON_LIBS)
```

- [ ] **Step 4: Write the implementation**

Create `common/iot_device_core/src/iot_dispatch.c`:

```c
#include "iot_dispatch.h"
#include <string.h>

typedef struct {
    const char *event;
    iot_event_handler_t handler;
} dispatch_entry_t;

static dispatch_entry_t s_table[IOT_DISPATCH_MAX];
static int s_count = 0;

bool iot_dispatch_register(const char *event, iot_event_handler_t h) {
    if (event == NULL || h == NULL || s_count >= IOT_DISPATCH_MAX) {
        return false;
    }
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].event, event) == 0) {
            return false;   // 重复
        }
    }
    s_table[s_count].event = event;
    s_table[s_count].handler = h;
    s_count++;
    return true;
}

iot_event_handler_t iot_dispatch_lookup(const char *event) {
    if (event == NULL) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].event, event) == 0) {
            return s_table[i].handler;
        }
    }
    return NULL;
}

void iot_dispatch_reset(void) {
    s_count = 0;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd common/iot_device_core/test/host && make test_dispatch`
Expected: PASS — prints `test_dispatch OK`.

- [ ] **Step 6: Run the full host suite**

Run: `cd common/iot_device_core/test/host && make`
Expected: both `test_envelope OK` and `test_dispatch OK`.

- [ ] **Step 7: Commit**

```bash
git add common/iot_device_core/src/iot_dispatch.* common/iot_device_core/test/host/test_dispatch.c common/iot_device_core/test/host/Makefile
git commit -m "feat(iot_device_core): add event dispatch table with host tests"
```

---

## Task 4: Core device runtime — SRP + CoAP + worker task

**Files:**
- Create: `common/iot_device_core/src/iot_device_eui64.h`
- Create: `common/iot_device_core/src/iot_device_eui64.c`
- Modify: `common/iot_device_core/src/iot_device_core.c` (replace stubs)
- Modify: `common/iot_device_core/include/iot_device_core.h` (wire `register_handler` to dispatch)

**Interfaces:**
- Consumes: `iot_envelope_*` (Task 2), `iot_dispatch_*` (Task 3).
- Produces: working `iot_device_core_start()` and `iot_device_register_handler()`. No new public symbols.

This task is device-only (OpenThread + FreeRTOS); verified by build (Task 8) and monitor (Task 11), not host tests.

- [ ] **Step 1: Add EUI64 helper (copied pattern)**

Create `common/iot_device_core/src/iot_device_eui64.h`:

```c
#pragma once
#include <stdint.h>
// 8 字节 EUI64 -> 16 位小写十六进制 + '\0'(out 至少 17 字节)。
void iot_eui64_to_string(const uint8_t in[8], char out[17]);
```

Create `common/iot_device_core/src/iot_device_eui64.c`:

```c
#include "iot_device_eui64.h"
void iot_eui64_to_string(const uint8_t in[8], char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = hexd[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hexd[in[i] & 0xf];
    }
    out[16] = '\0';
}
```

- [ ] **Step 2: Rewrite `iot_device_core.c` — full runtime**

Replace the entire contents of `common/iot_device_core/src/iot_device_core.c`:

```c
#include "iot_device_core.h"
#include "iot_dispatch.h"
#include "iot_envelope.h"
#include "iot_device_eui64.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/instance.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "openthread/platform/radio.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"

#define TAG "iot_device_core"
#define REQID_MAX        64
#define EVENT_MAX        64
#define COAP_PAYLOAD_MAX 512

static char s_eui64_str[17];
static QueueHandle_t s_cmd_queue = NULL;

// 投递到 worker 的命令。data 为堆上 JSON 字符串(worker 负责 free)。
typedef struct {
    char reqid[REQID_MAX];
    char event[EVENT_MAX];
    char *data_json;     // {reqid,event,data} 原始 payload 的副本
    bool via_multicast;
} cmd_msg_t;

// 公共 API：注册转发到 dispatch 表
bool iot_device_register_handler(const char *event, iot_event_handler_t handler) {
    return iot_dispatch_register(event, handler);
}

// ---- SRP 自动注册(EUI64 作 host) ----
static void srp_autostart_cb(const otSockAddr *server, void *ctx) {
    (void)server; (void)ctx;
    ESP_LOGI(TAG, "SRP auto-start: host=%s", s_eui64_str);
}

// 调用者须已持 OT 锁。
static void srp_register(otInstance *inst) {
    uint8_t eui[8];
    otPlatRadioGetIeeeEui64(inst, eui);
    iot_eui64_to_string(eui, s_eui64_str);

    uint16_t size = 0;
    char *host = otSrpClientBuffersGetHostNameString(inst, &size);
    strncpy(host, s_eui64_str, size - 1); host[size - 1] = '\0';
    otSrpClientSetHostName(inst, host);
    otSrpClientEnableAutoHostAddress(inst);

    otSrpClientBuffersServiceEntry *entry = otSrpClientBuffersAllocateService(inst);
    if (entry == NULL) { ESP_LOGE(TAG, "SRP alloc failed"); return; }
    char *iname = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
    strncpy(iname, s_eui64_str, size - 1); iname[size - 1] = '\0';
    char *sname = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
    strncpy(sname, CONFIG_IOT_CORE_SERVICE_NAME, size - 1); sname[size - 1] = '\0';
    entry->mService.mPort = CONFIG_IOT_CORE_COAP_PORT;

    if (otSrpClientAddService(inst, &entry->mService) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "SRP add service failed"); return;
    }
    otSrpClientEnableAutoStartMode(inst, srp_autostart_cb, NULL);
    ESP_LOGI(TAG, "SRP queued host/instance=%s service=%s", s_eui64_str, CONFIG_IOT_CORE_SERVICE_NAME);
}

// ---- 上报：单播 NON POST 到 BR ack 资源。调用者须已持 OT 锁。 ----
static void report_response(const char *json) {
    otInstance *inst = esp_openthread_get_instance();
    const otSockAddr *server = otSrpClientGetServerAddress(inst);
    if (server == NULL) { ESP_LOGW(TAG, "report: no SRP server yet"); return; }

    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) return;
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_IOT_CORE_ACK_URI);
    otCoapMessageSetPayloadMarker(msg);
    otMessageAppend(msg, json, (uint16_t)strlen(json));

    otMessageInfo info; memset(&info, 0, sizeof(info));
    info.mPeerAddr = server->mAddress;
    info.mPeerPort = CONFIG_IOT_CORE_COAP_PORT;
    if (otCoapSendRequest(inst, msg, &info, NULL, NULL) != OT_ERROR_NONE) {
        otMessageFree(msg);
        ESP_LOGE(TAG, "report send failed");
    }
}

// ---- 命令 worker 任务 ----
static void process_cmd(const cmd_msg_t *m) {
    // 组播加 0~500ms 抖动，避免响应风暴
    if (m->via_multicast) {
        vTaskDelay(pdMS_TO_TICKS(esp_random() % 501));
    }
    // 解析 data
    char reqid[REQID_MAX], event[EVENT_MAX];
    cJSON *data = NULL;
    int code;
    cJSON *resp_data = cJSON_CreateObject();
    if (!iot_envelope_parse(m->data_json, reqid, sizeof(reqid), event, sizeof(event), &data)) {
        ESP_LOGW(TAG, "worker: bad envelope"); 
        cJSON_Delete(resp_data);
        return;   // 无法解析则无法回执(缺 reqid/event)
    }
    iot_event_handler_t h = iot_dispatch_lookup(event);
    if (h == NULL) {
        code = IOT_CODE_UNSUPPORTED;
    } else {
        code = h(data, resp_data);   // 可阻塞；handler 内部若需 OT API 自行持锁
    }
    char *out = iot_envelope_build_resp(reqid, s_eui64_str, event, code, resp_data);
    cJSON_Delete(data);
    cJSON_Delete(resp_data);
    if (out != NULL) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        report_response(out);
        esp_openthread_lock_release();
        free(out);
    }
}

static void worker_task(void *arg) {
    (void)arg;
    cmd_msg_t m;
    for (;;) {
        if (xQueueReceive(s_cmd_queue, &m, portMAX_DELAY) == pdTRUE) {
            process_cmd(&m);
            free(m.data_json);
        }
    }
}

// ---- CoAP ctrl 资源：仅入队，绝不阻塞 OT 任务 ----
static int coap_read_payload(otMessage *msg, char *buf, int cap) {
    uint16_t off = otMessageGetOffset(msg);
    uint16_t tot = otMessageGetLength(msg);
    int len = (int)tot - (int)off;
    if (len < 0) len = 0;
    if (len > cap - 1) len = cap - 1;
    int rd = otMessageRead(msg, off, buf, len);
    buf[rd] = '\0';
    return rd;
}

static void ctrl_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx;
    char payload[COAP_PAYLOAD_MAX];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    if (len <= 0 || s_cmd_queue == NULL) return;

    cmd_msg_t m;
    memset(&m, 0, sizeof(m));
    m.data_json = malloc(len + 1);
    if (m.data_json == NULL) { ESP_LOGE(TAG, "ctrl: oom"); return; }
    memcpy(m.data_json, payload, len);
    m.data_json[len] = '\0';
    m.via_multicast = (info->mSockAddr.mFields.m8[0] == 0xff);

    if (xQueueSend(s_cmd_queue, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cmd queue full, drop");
        free(m.data_json);
    }
}

static otCoapResource s_ctrl_resource = {
    .mUriPath = CONFIG_IOT_CORE_CTRL_URI,
    .mHandler = ctrl_handler,
    .mContext = NULL,
    .mNext = NULL,
};

void iot_device_core_start(void) {
    s_cmd_queue = xQueueCreate(CONFIG_IOT_CORE_WORKER_QUEUE_LEN, sizeof(cmd_msg_t));
    if (s_cmd_queue == NULL) { ESP_LOGE(TAG, "queue create failed"); return; }
    xTaskCreate(worker_task, "iot_worker", CONFIG_IOT_CORE_WORKER_STACK, NULL, 5, NULL);

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    srp_register(inst);
    if (otCoapStart(inst, CONFIG_IOT_CORE_COAP_PORT) == OT_ERROR_NONE) {
        otCoapAddResource(inst, &s_ctrl_resource);
        ESP_LOGI(TAG, "CoAP started, resource '%s'", CONFIG_IOT_CORE_CTRL_URI);
    } else {
        ESP_LOGE(TAG, "CoAP start failed");
    }
    otIp6Address maddr;
    if (otIp6AddressFromString(CONFIG_IOT_CORE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
        otIp6SubscribeMulticastAddress(inst, &maddr);
        ESP_LOGI(TAG, "subscribed multicast %s", CONFIG_IOT_CORE_MULTICAST_ADDR);
    }
    esp_openthread_lock_release();
}
```

Note the `m.event`/`m.reqid` fields are unused in this pass (parsing happens in the worker for simplicity — the whole payload is forwarded and reparsed). They stay in the struct for a future optimization; keeping them costs nothing and avoids reshaping the queue message later. This is intentional, not a placeholder.

- [ ] **Step 3: Verify no host regressions**

Run: `cd common/iot_device_core/test/host && make`
Expected: `test_envelope OK` and `test_dispatch OK` still pass (this task didn't touch pure logic).

- [ ] **Step 4: Commit**

```bash
git add common/iot_device_core/src/iot_device_eui64.* common/iot_device_core/src/iot_device_core.c
git commit -m "feat(iot_device_core): SRP register, CoAP server, command worker task"
```

---

## Task 5: Capability — iot_cap_switch

**Files:**
- Create: `common/iot_cap_switch/CMakeLists.txt`
- Create: `common/iot_cap_switch/idf_component.yml`
- Create: `common/iot_cap_switch/include/iot_cap_switch.h`
- Create: `common/iot_cap_switch/src/iot_cap_switch.c`

**Interfaces:**
- Consumes: `iot_device_register_handler`, `iot_event_handler_t`, `IOT_CODE_*` from `iot_device_core.h`.
- Produces: `void iot_cap_switch_init(void)` — registers `"switch"`.

Device-only; verified by build + monitor.

- [ ] **Step 1: Write the header**

Create `common/iot_cap_switch/include/iot_cap_switch.h`:

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 注册 "switch" event。须在 iot_device_core_start() 之后调用。
void iot_cap_switch_init(void);
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write the implementation**

Create `common/iot_cap_switch/src/iot_cap_switch.c`:

```c
#include "iot_cap_switch.h"
#include "iot_device_core.h"
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define TAG "iot_cap_switch"

// hold 自动回复：单槽(命令低频)。定时器承载，不阻塞 worker 过久。
static TimerHandle_t s_hold_timer = NULL;
static int s_hold_gpio = -1;
static int s_hold_restore_level = 0;

static void hold_timer_cb(TimerHandle_t t) {
    (void)t;
    if (s_hold_gpio >= 0) {
        gpio_set_level(s_hold_gpio, s_hold_restore_level);
    }
}

static void ensure_output(int gpio) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT,   // INPUT_OUTPUT 便于回读
        .pull_up_en = 0, .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static int switch_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jaction = cJSON_GetObjectItem(data, "action");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsString(jaction)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint;
    int level;
    if (strcmp(jaction->valuestring, "on") == 0) {
        level = 1;
    } else if (strcmp(jaction->valuestring, "off") == 0) {
        level = 0;
    } else {
        return IOT_CODE_PARAM;
    }

    cJSON *jdelay = cJSON_GetObjectItem(data, "delay");
    cJSON *jhold  = cJSON_GetObjectItem(data, "hold");
    int delay_ms = cJSON_IsNumber(jdelay) ? jdelay->valueint : 0;
    int hold_ms  = cJSON_IsNumber(jhold)  ? jhold->valueint  : 0;

    ensure_output(gpio);
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));   // 在 worker 任务里，允许阻塞
    }
    gpio_set_level(gpio, level);

    if (hold_ms > 0) {
        s_hold_gpio = gpio;
        s_hold_restore_level = level ? 0 : 1;   // 回到相反态
        if (s_hold_timer == NULL) {
            s_hold_timer = xTimerCreate("sw_hold", pdMS_TO_TICKS(hold_ms), pdFALSE, NULL, hold_timer_cb);
        } else {
            xTimerChangePeriod(s_hold_timer, pdMS_TO_TICKS(hold_ms), 0);
        }
        if (s_hold_timer != NULL) xTimerStart(s_hold_timer, 0);
    }

    cJSON_AddNumberToObject(resp_data, "gpio", gpio);
    cJSON_AddStringToObject(resp_data, "status", level ? "on" : "off");
    return IOT_CODE_OK;
}

void iot_cap_switch_init(void) {
    if (!iot_device_register_handler("switch", switch_handler)) {
        ESP_LOGE(TAG, "register switch failed");
    }
}
```

- [ ] **Step 3: Write CMakeLists.txt**

Create `common/iot_cap_switch/CMakeLists.txt`:

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES iot_device_core
                       PRIV_REQUIRES espressif__cjson driver freertos)
```

- [ ] **Step 4: Write idf_component.yml**

Create `common/iot_cap_switch/idf_component.yml`:

```yaml
dependencies:
  espressif/cjson: "^1.7.19"
  idf:
    version: ">=5.1.0"
```

- [ ] **Step 5: Commit**

```bash
git add common/iot_cap_switch/
git commit -m "feat(iot_cap_switch): switch event capability (gpio/action/delay/hold)"
```

---

## Task 6: Capability — iot_cap_pwm_set

**Files:**
- Create: `common/iot_cap_pwm_set/CMakeLists.txt`
- Create: `common/iot_cap_pwm_set/idf_component.yml`
- Create: `common/iot_cap_pwm_set/include/iot_cap_pwm_set.h`
- Create: `common/iot_cap_pwm_set/src/iot_cap_pwm_set.c`

**Interfaces:**
- Consumes: `iot_device_register_handler`, `IOT_CODE_*`.
- Produces: `void iot_cap_pwm_set_init(void)` — registers `"pwm_set"`.

- [ ] **Step 1: Write the header**

Create `common/iot_cap_pwm_set/include/iot_cap_pwm_set.h`:

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 注册 "pwm_set" event。须在 iot_device_core_start() 之后调用。
void iot_cap_pwm_set_init(void);
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write the implementation**

Create `common/iot_cap_pwm_set/src/iot_cap_pwm_set.c`:

```c
#include "iot_cap_pwm_set.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define TAG "iot_cap_pwm_set"
#define PWM_MODE       LEDC_LOW_SPEED_MODE
#define PWM_RES        LEDC_TIMER_10_BIT       // 0..1023
#define PWM_MAX_DUTY   1023
#define PWM_TIMER      LEDC_TIMER_0

// gpio→channel 分配表(组件级 static 状态)。
static int s_gpio_of_channel[LEDC_CHANNEL_MAX];
static bool s_channel_used[LEDC_CHANNEL_MAX];
static bool s_timer_ready = false;

static int alloc_channel(int gpio) {
    for (int c = 0; c < LEDC_CHANNEL_MAX; c++) {
        if (s_channel_used[c] && s_gpio_of_channel[c] == gpio) return c;  // 复用
    }
    for (int c = 0; c < LEDC_CHANNEL_MAX; c++) {
        if (!s_channel_used[c]) { s_channel_used[c] = true; s_gpio_of_channel[c] = gpio; return c; }
    }
    return -1;   // 耗尽
}

static int pwm_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jfreq = cJSON_GetObjectItem(data, "freq");
    cJSON *jduty = cJSON_GetObjectItem(data, "duty");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsNumber(jfreq) || !cJSON_IsNumber(jduty)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint, freq = jfreq->valueint, duty = jduty->valueint;
    if (freq <= 0 || duty < 0 || duty > 100) {
        return IOT_CODE_PARAM;
    }

    if (!s_timer_ready) {
        ledc_timer_config_t tc = {
            .speed_mode = PWM_MODE, .duty_resolution = PWM_RES,
            .timer_num = PWM_TIMER, .freq_hz = freq, .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tc) != ESP_OK) return IOT_CODE_HW;
        s_timer_ready = true;
    } else {
        ledc_set_freq(PWM_MODE, PWM_TIMER, freq);
    }

    int ch = alloc_channel(gpio);
    if (ch < 0) return IOT_CODE_HW;   // 通道耗尽

    uint32_t raw = (uint32_t)((duty * PWM_MAX_DUTY) / 100);
    ledc_channel_config_t cc = {
        .gpio_num = gpio, .speed_mode = PWM_MODE, .channel = ch,
        .timer_sel = PWM_TIMER, .duty = raw, .hpoint = 0, .intr_type = LEDC_INTR_DISABLE,
    };
    if (ledc_channel_config(&cc) != ESP_OK) return IOT_CODE_HW;
    ledc_set_duty(PWM_MODE, ch, raw);
    ledc_update_duty(PWM_MODE, ch);

    cJSON_AddNumberToObject(resp_data, "gpio", gpio);
    cJSON_AddNumberToObject(resp_data, "freq", freq);
    cJSON_AddNumberToObject(resp_data, "duty", duty);
    return IOT_CODE_OK;
}

void iot_cap_pwm_set_init(void) {
    if (!iot_device_register_handler("pwm_set", pwm_handler)) {
        ESP_LOGE(TAG, "register pwm_set failed");
    }
}
```

- [ ] **Step 3: Write CMakeLists.txt**

Create `common/iot_cap_pwm_set/CMakeLists.txt`:

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES iot_device_core
                       PRIV_REQUIRES espressif__cjson driver)
```

- [ ] **Step 4: Write idf_component.yml**

Create `common/iot_cap_pwm_set/idf_component.yml`:

```yaml
dependencies:
  espressif/cjson: "^1.7.19"
  idf:
    version: ">=5.1.0"
```

- [ ] **Step 5: Commit**

```bash
git add common/iot_cap_pwm_set/
git commit -m "feat(iot_cap_pwm_set): pwm_set event capability (LEDC, channel alloc)"
```

---

## Task 7: Capability — iot_cap_adc_read

**Files:**
- Create: `common/iot_cap_adc_read/CMakeLists.txt`
- Create: `common/iot_cap_adc_read/idf_component.yml`
- Create: `common/iot_cap_adc_read/include/iot_cap_adc_read.h`
- Create: `common/iot_cap_adc_read/src/iot_cap_adc_read.c`

**Interfaces:**
- Consumes: `iot_device_register_handler`, `IOT_CODE_*`.
- Produces: `void iot_cap_adc_read_init(void)` — registers `"adc_read"`.

- [ ] **Step 1: Write the header**

Create `common/iot_cap_adc_read/include/iot_cap_adc_read.h`:

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 注册 "adc_read" event。须在 iot_device_core_start() 之后调用。
void iot_cap_adc_read_init(void);
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write the implementation**

Create `common/iot_cap_adc_read/src/iot_cap_adc_read.c`:

```c
#include "iot_cap_adc_read.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define TAG "iot_cap_adc_read"
#define ADC_UNIT      ADC_UNIT_1
#define ADC_ATTEN     ADC_ATTEN_DB_12

// 组件级 static 句柄，首次使用创建后复用。
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_cali_ok = false;

static bool ensure_adc(void) {
    if (s_adc != NULL) return true;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; return false; }

    adc_cali_curve_fitting_config_t ccfg = {
        .unit_id = ADC_UNIT, .atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&ccfg, &s_cali) == ESP_OK);
    return true;
}

static int adc_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jch = cJSON_GetObjectItem(data, "channel");
    if (!cJSON_IsNumber(jch)) return IOT_CODE_PARAM;
    int channel = jch->valueint;
    if (channel < 0 || channel >= SOC_ADC_CHANNEL_NUM(ADC_UNIT)) return IOT_CODE_PARAM;

    if (!ensure_adc()) return IOT_CODE_HW;

    adc_oneshot_chan_cfg_t chcfg = { .atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, channel, &chcfg) != ESP_OK) return IOT_CODE_HW;

    int raw = 0;
    if (adc_oneshot_read(s_adc, channel, &raw) != ESP_OK) return IOT_CODE_HW;

    double voltage;
    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
        voltage = mv / 1000.0;
    } else {
        voltage = (raw * 3.3) / 4095.0;   // 线性估算兜底
    }
    cJSON_AddNumberToObject(resp_data, "channel", channel);
    cJSON_AddNumberToObject(resp_data, "raw_val", raw);
    cJSON_AddNumberToObject(resp_data, "voltage", voltage);
    return IOT_CODE_OK;
}

void iot_cap_adc_read_init(void) {
    if (!iot_device_register_handler("adc_read", adc_handler)) {
        ESP_LOGE(TAG, "register adc_read failed");
    }
}
```

- [ ] **Step 3: Write CMakeLists.txt**

Create `common/iot_cap_adc_read/CMakeLists.txt`:

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES iot_device_core
                       PRIV_REQUIRES espressif__cjson esp_adc)
```

- [ ] **Step 4: Write idf_component.yml**

Create `common/iot_cap_adc_read/idf_component.yml`:

```yaml
dependencies:
  espressif/cjson: "^1.7.19"
  idf:
    version: ">=5.1.0"
```

- [ ] **Step 5: Commit**

```bash
git add common/iot_cap_adc_read/
git commit -m "feat(iot_cap_adc_read): adc_read event capability (oneshot + calibration)"
```

---

## Task 8: Rewire ot_iot_device demo onto the components

**Files:**
- Modify: `ot_iot_device/main/esp_ot_iot_device.c` (`:36` include, `:80` call)
- Modify: `ot_iot_device/main/CMakeLists.txt`
- Modify: `ot_iot_device/main/idf_component.yml`
- Delete: `ot_iot_device/main/iot_device.c`, `iot_device.h`, `device_switch.c`, `device_switch.h`, `device_eui64.c`, `device_eui64.h`

**Interfaces:**
- Consumes: `iot_device_core_start`, `iot_cap_switch_init`, `iot_cap_pwm_set_init`, `iot_cap_adc_read_init`.

This is the first full device build — verifies all components compile and link together.

- [ ] **Step 1: Delete old flat sources**

```bash
git rm ot_iot_device/main/iot_device.c ot_iot_device/main/iot_device.h \
       ot_iot_device/main/device_switch.c ot_iot_device/main/device_switch.h \
       ot_iot_device/main/device_eui64.c ot_iot_device/main/device_eui64.h
```

- [ ] **Step 2: Update `esp_ot_iot_device.c` include**

Replace line `#include "iot_device.h"` (at `:36`) with:

```c
#include "iot_device_core.h"
#include "iot_cap_switch.h"
#include "iot_cap_pwm_set.h"
#include "iot_cap_adc_read.h"
```

- [ ] **Step 3: Update the startup call**

Replace `iot_device_start();` (at `:80`) with:

```c
    iot_device_core_start();
    iot_cap_switch_init();
    iot_cap_pwm_set_init();
    iot_cap_adc_read_init();
```

- [ ] **Step 4: Update `main/CMakeLists.txt`**

Replace entire file with:

```cmake
idf_component_register(SRCS "esp_ot_iot_device.c"
                       INCLUDE_DIRS ".")
```

(Per Global Constraints, `main` declares no REQUIRES — MINIMAL_BUILD makes components visible.)

- [ ] **Step 5: Update `main/idf_component.yml`**

Replace entire file with:

```yaml
## IDF Component Manager Manifest File
dependencies:
  espressif/esp_ot_cli_extension:
    version: "~2.0.0"
  espressif/cjson: "^1.7.19"
  iot_device_core:
    path: ../../common/iot_device_core
  iot_cap_switch:
    path: ../../common/iot_cap_switch
  iot_cap_pwm_set:
    path: ../../common/iot_cap_pwm_set
  iot_cap_adc_read:
    path: ../../common/iot_cap_adc_read
  ot_led:
    path: ${IDF_PATH}/examples/openthread/ot_common_components/ot_led
  ot_examples_common:
    path: ${IDF_PATH}/examples/openthread/ot_common_components/ot_examples_common
  esp-qa/coexist-cmd:
    version: "~0.0.1"
  espressif/led_strip: "^3.0.0"
  idf:
    version: ">=4.1.0"
```

- [ ] **Step 6: Build (ESP-IDF PowerShell/CMD, not Git Bash)**

Ask the user to run in an ESP-IDF terminal from `ot_iot_device/`:

```
idf.py build
```

Expected: build succeeds; linker resolves `iot_device_core_start`, `iot_cap_switch_init`, `iot_cap_pwm_set_init`, `iot_cap_adc_read_init`. If MINIMAL_BUILD drops a component, verify each is listed in `main/idf_component.yml`.

- [ ] **Step 7: Commit**

```bash
git add ot_iot_device/main/
git commit -m "refactor(ot_iot_device): wire core + switch/pwm/adc capabilities, drop old {cmd} protocol"
```

---

## Task 9: BR registry_list response builder (pure, host-TDD)

**Files:**
- Modify: `common/mqtt_ot_bridge/src/bridge_registry_json.h`
- Modify: `common/mqtt_ot_bridge/src/bridge_registry_json.c`
- Modify: `common/mqtt_ot_bridge/test/host/Makefile`
- Test: `common/mqtt_ot_bridge/test/host/test_registry_json.c` (extend)

**Interfaces:**
- Consumes: existing `bridge_dev_entry_t { char eui64[17]; char ipv6[46]; char service[64]; }`.
- Produces: `char *bridge_registry_list_resp_to_json(const char *reqid, const char *br_eui64, const bridge_dev_entry_t *entries, size_t count)` — builds full response envelope `{reqid,eui64,event:"registry_list_resp",code:0,msg:"success",data:{list:[...]}}`. Malloc'd; caller frees. NULL on alloc failure.

- [ ] **Step 1: Read the existing test to append correctly**

Run: `cat common/mqtt_ot_bridge/test/host/test_registry_json.c`
Note its `main()` structure — you will add assertions before its final `printf`/`return`.

- [ ] **Step 2: Write the failing test additions**

In `common/mqtt_ot_bridge/test/host/test_registry_json.c`, add this function above `main()` and call it from `main()` before the success print:

```c
static void test_registry_list_resp(void) {
    bridge_dev_entry_t e[2] = {
        { "744dbdfffe664fc4", "fd32::1", "_iot._udp" },
        { "744dbdfffe621c77", "fd32::2", "_iot._udp" },
    };
    char *out = bridge_registry_list_resp_to_json("batch-0001", "0000000000000000", e, 2);
    assert(out != NULL);
    cJSON *p = cJSON_Parse(out);
    assert(strcmp(cJSON_GetObjectItem(p, "reqid")->valuestring, "batch-0001") == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "event")->valuestring, "registry_list_resp") == 0);
    assert(cJSON_GetObjectItem(p, "code")->valueint == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "msg")->valuestring, "success") == 0);
    cJSON *list = cJSON_GetObjectItem(cJSON_GetObjectItem(p, "data"), "list");
    assert(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 2);
    cJSON *first = cJSON_GetArrayItem(list, 0);
    assert(strcmp(cJSON_GetObjectItem(first, "eui64")->valuestring, "744dbdfffe664fc4") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "ipv6")->valuestring, "fd32::1") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "service")->valuestring, "_iot._udp") == 0);
    cJSON_Delete(p);
    free(out);
}
```

Ensure `#include <stdlib.h>` (for `free`) and `#include "cJSON.h"` are present at the top of the test file; add them if missing. Add the `test_registry_list_resp();` call inside `main()` before its final print.

- [ ] **Step 3: Run test to verify it fails**

Run: `cd common/mqtt_ot_bridge/test/host && make test_registry_json`
Expected: FAIL — undefined `bridge_registry_list_resp_to_json`.

- [ ] **Step 4: Declare the new function in the header**

In `common/mqtt_ot_bridge/src/bridge_registry_json.h`, add after the existing declaration:

```c
// 生成 registry_list 完整响应信封:
// {reqid,eui64,event:"registry_list_resp",code:0,msg:"success",data:{list:[...]}}
// 调用者 free。失败返回 NULL。
char *bridge_registry_list_resp_to_json(const char *reqid, const char *br_eui64,
                                        const bridge_dev_entry_t *entries, size_t count);
```

- [ ] **Step 5: Implement it**

In `common/mqtt_ot_bridge/src/bridge_registry_json.c`, add at the end:

```c
char *bridge_registry_list_resp_to_json(const char *reqid, const char *br_eui64,
                                        const bridge_dev_entry_t *entries, size_t count) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "reqid", reqid ? reqid : "");
    cJSON_AddStringToObject(root, "eui64", br_eui64 ? br_eui64 : "");
    cJSON_AddStringToObject(root, "event", "registry_list_resp");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddStringToObject(root, "msg", "success");

    cJSON *data = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "eui64", entries[i].eui64);
        cJSON_AddStringToObject(o, "ipv6", entries[i].ipv6);
        cJSON_AddStringToObject(o, "service", entries[i].service);
        cJSON_AddItemToArray(list, o);
    }
    cJSON_AddItemToObject(data, "list", list);
    cJSON_AddItemToObject(root, "data", data);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd common/mqtt_ot_bridge/test/host && make test_registry_json`
Expected: PASS — prints `test_registry_json OK`.

- [ ] **Step 7: Run the full bridge host suite (no regressions)**

Run: `cd common/mqtt_ot_bridge/test/host && make`
Expected: `test_eui64 OK`, `test_topic OK`, `test_registry_json OK`.

- [ ] **Step 8: Commit**

```bash
git add common/mqtt_ot_bridge/src/bridge_registry_json.* common/mqtt_ot_bridge/test/host/test_registry_json.c
git commit -m "feat(mqtt_ot_bridge): add registry_list response envelope builder with host test"
```

---

## Task 10: BR runtime — cmd/resp topic, cmd/registry subscribe + answer

**Files:**
- Modify: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`

**Interfaces:**
- Consumes: `bridge_registry_list_resp_to_json` (Task 9); existing in-file statics `registry_collect()`, `s_client`, `REGISTRY_MAX_DEVICES`. BR's own EUI64 helper is added in this task (no such helper pre-exists).

Device-only (BR); verified by build (this task's step) + monitor (Task 11).

- [ ] **Step 1: Rename uplink topic to `cmd/resp`**

In `publish_uplink()`, change the topic line:

```c
    snprintf(t, sizeof(t), "%s/cmd/resp", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
```

(was `%s/dev/response`). Leave the retained `dev/registry` publish in `publish_registry()` unchanged.

- [ ] **Step 2: Add BR's own EUI64 accessor**

Near the top helpers (after `topic_suffix_after_prefix`), add:

```c
// BR 自身 EUI64(小写十六进制)。懒初始化，须已持 OT 锁调用。
static const char *br_eui64_str(void) {
    static char s[17] = {0};
    if (s[0] == '\0') {
        uint8_t eui[8];
        otPlatRadioGetIeeeEui64(esp_openthread_get_instance(), eui);
        static const char *hexd = "0123456789abcdef";
        for (int i = 0; i < 8; i++) {
            s[i*2]   = hexd[(eui[i] >> 4) & 0xf];
            s[i*2+1] = hexd[eui[i] & 0xf];
        }
        s[16] = '\0';
    }
    return s;
}
```

Add `#include "openthread/platform/radio.h"` to the includes block if not present (it provides `otPlatRadioGetIeeeEui64`).

- [ ] **Step 3: Add the registry_list handler**

Add a function (after `publish_registry()`):

```c
// 解析 cmd/registry 上的 registry_list 指令，查 SRP 表并发布响应到 cmd/resp。
static void handle_registry_cmd(const char *data, int data_len) {
    // 取 reqid(可选)
    char reqid[64] = "";
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root != NULL) {
        cJSON *jr = cJSON_GetObjectItem(root, "reqid");
        if (cJSON_IsString(jr) && jr->valuestring) {
            strncpy(reqid, jr->valuestring, sizeof(reqid) - 1);
            reqid[sizeof(reqid) - 1] = '\0';
        }
        cJSON_Delete(root);
    }

    bridge_dev_entry_t *entries = calloc(REGISTRY_MAX_DEVICES, sizeof(bridge_dev_entry_t));
    if (entries == NULL) { ESP_LOGE(TAG, "registry cmd: alloc failed"); return; }
    size_t count = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    registry_collect(entries, REGISTRY_MAX_DEVICES, &count);
    const char *br_eui = br_eui64_str();
    esp_openthread_lock_release();

    char *json = bridge_registry_list_resp_to_json(reqid, br_eui, entries, count);
    free(entries);
    if (json == NULL) return;
    if (s_client != NULL) {
        char t[128];
        snprintf(t, sizeof(t), "%s/cmd/resp", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
        esp_mqtt_client_publish(s_client, t, json, 0, 0, 0);
        ESP_LOGI(TAG, "registry_list answered: %d devices", (int)count);
    }
    free(json);
}
```

Add `#include "cJSON.h"` to the includes block if not present.

- [ ] **Step 4: Route `cmd/registry` in the MQTT data event**

The current `MQTT_EVENT_DATA` case computes `suffix` via `topic_suffix_after_prefix` (which keys on `/cmd/`). `cmd/registry` also matches `/cmd/` so `suffix` becomes `"registry"`. Handle it before the generic downlink path. In the `MQTT_EVENT_DATA` case, replace:

```c
        const char *suffix = topic_suffix_after_prefix(topic);
        if (suffix != NULL) {
            handle_downlink(suffix, event->data, event->data_len);
        }
```

with:

```c
        const char *suffix = topic_suffix_after_prefix(topic);
        if (suffix != NULL) {
            if (strcmp(suffix, "registry") == 0) {
                handle_registry_cmd(event->data, event->data_len);
            } else {
                handle_downlink(suffix, event->data, event->data_len);
            }
        }
```

- [ ] **Step 5: Subscribe `cmd/registry`**

In `subscribe_all()`, add after the multicast subscribe:

```c
    snprintf(t, sizeof(t), "%s/cmd/registry", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
```

- [ ] **Step 6: Build the BR (ESP-IDF PowerShell/CMD)**

Ask the user to run from `basic_thread_border_router/`:

```
idf.py build
```

Expected: build succeeds. If `otPlatRadioGetIeeeEui64` or `cJSON` is unresolved, confirm the includes from Steps 2–3 were added.

- [ ] **Step 7: Commit**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c
git commit -m "feat(mqtt_ot_bridge): cmd/resp uplink, cmd/registry subscribe + registry_list answer"
```

---

## Task 11: End-to-end hardware verification

**Files:** none (verification only). Requires 1× ESP32-C6 BR + ≥2× ESP32-H2 devices, an MQTT broker, and an MQTT client (e.g. `mosquitto_pub`/`mosquitto_sub`).

- [ ] **Step 1: Flash and monitor**

BR (from `basic_thread_border_router/`, ESP-IDF terminal): `idf.py -p <COM_BR> flash monitor`
Each device (from `ot_iot_device/`): `idf.py -p <COM_DEV> flash monitor`
Expected: devices log `SRP queued ... service=_iot._udp` and `CoAP started, resource 'ctrl'`; BR logs periodic `published registry: N devices`.

- [ ] **Step 2: Verify unicast switch round-trip**

Subscribe: `mosquitto_sub -t 'otbr/cmd/resp' -v`
Publish (replace `<eui64>` with a device's): 
`mosquitto_pub -t 'otbr/cmd/unicast/<eui64>' -m '{"reqid":"u1","event":"switch","data":{"gpio":8,"action":"on"}}'`
Expected on `cmd/resp`: `{"reqid":"u1","eui64":"<eui64>","event":"switch_resp","code":0,"msg":"success","data":{"gpio":8,"status":"on"}}`. Note: the generic `switch` capability drives a **raw GPIO level** (`gpio_set_level`), not the WS2812 strip the old firmware used — verify with a multimeter or a plain LED on the chosen GPIO, not the onboard addressable LED. Pick a free GPIO for a clean test if GPIO8 is wired to the WS2812.

- [ ] **Step 3: Verify multicast + jitter, no storm/duplication**

`mosquitto_pub -t 'otbr/cmd/multicast' -m '{"reqid":"m1","event":"switch","data":{"gpio":8,"action":"off"}}'`
Expected: each device emits exactly one `switch_resp` on `cmd/resp` (reqid `m1`), spread over ~0–500ms. No device reports twice.

- [ ] **Step 4: Verify pwm_set and adc_read + error codes**

`mosquitto_pub -t 'otbr/cmd/unicast/<eui64>' -m '{"reqid":"p1","event":"pwm_set","data":{"gpio":5,"freq":1000,"duty":60}}'`
Expected: `pwm_set_resp` with `data:{gpio:5,freq:1000,duty:60}`, code 0.
`mosquitto_pub -t 'otbr/cmd/unicast/<eui64>' -m '{"reqid":"a1","event":"adc_read","data":{"channel":0}}'`
Expected: `adc_read_resp` with `raw_val` and `voltage`, code 0.
Param error: `...'{"reqid":"e1","event":"switch","data":{"gpio":8}}'` (no action) → `switch_resp` code `-1`, msg `fail`.
Unsupported: `...'{"reqid":"e2","event":"servo_set","data":{}}'` → `servo_set_resp` code `-3`, msg `fail`.

- [ ] **Step 5: Verify registry_list**

`mosquitto_pub -t 'otbr/cmd/registry' -m '{"reqid":"r1","event":"registry_list","data":{}}'`
Expected on `cmd/resp`: `{"reqid":"r1","eui64":"<BR eui64>","event":"registry_list_resp","code":0,"msg":"success","data":{"list":[{eui64,ipv6,service},...]}}` listing all SRP-registered devices. Meanwhile a plain control command on `cmd/unicast/<eui64>` still works (confirms control path stayed passthrough).

- [ ] **Step 6: Update the memory note**

Append to `mqtt-ot-bridge` memory (or create a new memory) that the component refactor landed: `iot_device_core` + `iot_cap_*` on device side, BR migrated to `cmd/resp`/`cmd/registry`, `{reqid,event,data}` protocol. Record any hardware quirks found during Steps 2–5.

- [ ] **Step 7: Final commit (if any monitor-driven fixes were made)**

```bash
git add -A
git commit -m "test: e2e verification of iot device components + BR protocol migration"
```

---

## Notes for the implementer

- **Host tests are your fast loop.** Tasks 2, 3, 9 are pure C — run `make` in the relevant `test/host/` dir after every change. They need `gcc` + `libcjson` (`pkg-config --exists libcjson`). Install: `apt install libcjson-dev` / `brew install cjson`.
- **Device builds must run in ESP-IDF PowerShell/CMD**, never Git Bash (it errors "MSys/Mingw is no longer supported"). Steps 8/10-6/11 need the user to run `idf.py` and paste results.
- **The worker task is the safety mechanism.** Handlers run there and may block or take the OT lock; the CoAP callback only mallocs + enqueues. Never move handler work into `ctrl_handler`.
- **`main` never lists REQUIRES** — if a symbol from a component is "undefined", the fix is adding the component to `main/idf_component.yml`, not adding REQUIRES to `main/CMakeLists.txt`.
