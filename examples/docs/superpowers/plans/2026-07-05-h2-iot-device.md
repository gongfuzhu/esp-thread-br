# H2 IoT Device Firmware (Switch/LED) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 基于 `ot_cli` 派生一个 ESP32-H2 IoT 设备示例 `ot_iot_device`,作为 `mqtt_ot_bridge` 的受控端:入网后经 SRP 用 EUI64 注册自己,提供 `ctrl` CoAP 资源接收开关命令(单播 CON / 组播 NON),执行后单播 CoAP 上报到 BR 的 `ack` 资源,payload 用 cJSON 解析/构造并回带服务端下发的 reqid。

**Architecture:** 复制 `ot_cli` 的成熟 OT 启动骨架(不改原示例),在 `esp_openthread_start()` 之后追加一次 `iot_device_start()`。设备身份 = 出厂 EUI64(`otPlatRadioGetIeeeEui64`),既作 SRP host 名(16 位小写十六进制),也作 CoAP 响应里的自报 id。SRP 用 auto-start 模式,入网自动发现 BR 的 SRP server 并注册。CoAP server 注册 `ctrl` 资源;命令处理后订阅组播、单播回报。GPIO 驱动一个 LED/继电器代表开关状态。

**Tech Stack:** ESP-IDF v6.0.2、OpenThread(`otSrpClient*` / `otCoap*` / `otIp6*` / `otPlatRadioGetIeeeEui64`)、cJSON(`espressif/cjson` 托管组件)、`driver`(GPIO)。

## Global Constraints

- ESP-IDF 环境:`source /d/esp/v6.0.2/esp-idf/export.sh`;IDF 版本 **v6.0.2**。目标芯片 **esp32h2**。
- **`idf.py` 必须在 ESP-IDF PowerShell/CMD 运行,不能 Git Bash**(报 "MSys/Mingw is no longer supported")。本机需编译时由用户在 PowerShell 执行并回贴输出。
- **IDF v6 托管组件名**:cJSON 依赖声明 `espressif/cjson`,CMake REQUIRES 用 `espressif__cjson`,头 `#include "cJSON.h"`。
- **无主机端测试**:验证 = `idf.py build` + 设备端 monitor + 与 BR 联调。
- **不改动原 `ot_cli` 示例**:新建 `examples/ot_iot_device`(从 ot_cli 复制骨架)。
- 所有 OpenThread API 调用必须在 OT 任务上下文或持锁下进行:`esp_openthread_lock_acquire(portMAX_DELAY)` … `esp_openthread_lock_release()`。CoAP 资源回调在 OT 任务上下文中执行(已持锁),回调内**不可**再次 acquire。
- **与 BR 端(mqtt_ot_bridge)的硬契约**(来自 `common/mqtt_ot_bridge/README.md`):
  - SRP host full name 以 **16 位小写十六进制 EUI64** 开头(如 `1a2b3c4d5e6f7080`)。
  - CoAP 控制资源 Uri-Path = `ctrl`(对应 BR 的 `CONFIG_MQTT_OT_BRIDGE_COAP_URI`)。
  - 上报目标 = BR 的 `ack` 资源(对应 BR 的 `CONFIG_MQTT_OT_BRIDGE_ACK_URI`),CoAP POST,端口 5683。
  - 组播组 = `ff03::1`(对应 BR 的 `CONFIG_MQTT_OT_BRIDGE_MULTICAST_ADDR`)。
  - 组播命令执行后,上报前随机抖动 0~500ms。
  - payload 内 reqid 由服务端定义,设备原样回带。
- EUI64 文本格式:16 位小写十六进制,无分隔符。设备用 `otPlatRadioGetIeeeEui64` 取 8 字节,首字节 → 文本首字符。

---

## File Structure

```
examples/ot_iot_device/                    # 从 ot_cli 复制
├── CMakeLists.txt                          # 顶层(照抄 ot_cli，改 project 名)
├── partitions.csv                          # 照抄 ot_cli
├── sdkconfig.defaults                      # 照抄 ot_cli + 追加本示例配置
├── main/
│   ├── CMakeLists.txt                      # SRCS 增加新源文件 + REQUIRES
│   ├── idf_component.yml                   # 照抄 ot_cli + espressif/cjson
│   ├── esp_ot_config.h                     # 照抄 ot_cli
│   ├── esp_ot_iot_device.c                 # app_main：复制 ot_cli 的 app_main + 调 iot_device_start()
│   ├── iot_device.h                        # iot_device_start() 声明
│   ├── iot_device.c                        # SRP 注册 + CoAP server(ctrl) + 上报(ack) 编排
│   ├── device_eui64.c / .h                 # 纯逻辑：EUI64 字节 -> 16位小写十六进制(与 BR 端约定一致)
│   └── device_switch.c / .h                # GPIO 开关抽象：init/set/get
└── README.md                              # 设备说明 + 与 BR 的契约
```

拆分理由:`device_eui64`(纯逻辑,可复用 BR 端同款转换)、`device_switch`(硬件抽象,便于换 GPIO 或改虚拟)、`iot_device`(OT 编排)三者职责分明。

---

## Task 1: 从 ot_cli 派生工程骨架(编译验证)

**Files:**
- Create(复制自 ot_cli): `examples/ot_iot_device/CMakeLists.txt`、`partitions.csv`、`sdkconfig.defaults`、`main/CMakeLists.txt`、`main/idf_component.yml`、`main/esp_ot_config.h`
- Create: `examples/ot_iot_device/main/esp_ot_iot_device.c`(基于 ot_cli 的 esp_ot_cli.c)
- Create: `examples/ot_iot_device/main/iot_device.h`(占位声明)

**Interfaces:**
- Produces: `void iot_device_start(void);`(本任务仅声明 + 空实现桩,Task 3 起填充)。

- [ ] **Step 1: 复制 ot_cli 骨架文件**

在 bash(仅文件操作,不编译)执行:
```bash
cd /d/code/ot/esp-thread-br/examples
cp -r ot_cli ot_iot_device
cd ot_iot_device
# 删除 CI 变体与无关文件(保留核心)
rm -f sdkconfig.ci.* README.md
rm -f main/esp_ot_cli.c
rm -rf build managed_components sdkconfig
```

- [ ] **Step 2: 顶层 CMakeLists 改 project 名**

编辑 `examples/ot_iot_device/CMakeLists.txt`,把 `project(...)` 行改为:
```cmake
project(ot_iot_device)
```
(其余照抄 ot_cli 顶层 CMakeLists 内容;若复制后已存在,仅改 project 名。)

- [ ] **Step 3: 写 app_main(基于 ot_cli 的 esp_ot_cli.c)**

`examples/ot_iot_device/main/esp_ot_iot_device.c`:
```c
/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread IoT Device Example (switch/LED), controlled via mqtt_ot_bridge.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "ot_examples_common.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif

#include "iot_device.h"

#define TAG "ot_iot_device"

void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
    ESP_ERROR_CHECK(esp_netif_init());
#endif
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
#endif

    static esp_openthread_config_t config = {
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
#endif
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif
#if CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ot_network_auto_start();
#endif

    iot_device_start();
}
```

- [ ] **Step 4: 写占位头 + 空实现桩**

`examples/ot_iot_device/main/iot_device.h`:
```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 启动 IoT 设备逻辑：SRP 自动注册 + CoAP server(ctrl 资源)。
// 必须在 esp_openthread_start() 之后调用。
void iot_device_start(void);

#ifdef __cplusplus
}
#endif
```

临时在 `esp_ot_iot_device.c` 末尾加一个 weak 空桩以便本任务独立编译(Task 3 用真实实现替换,放在 iot_device.c):
```c
// 临时桩：Task 3 由 iot_device.c 提供真实实现(强符号覆盖)。
__attribute__((weak)) void iot_device_start(void) {}
```

- [ ] **Step 5: main/CMakeLists.txt 更新 SRCS**

`examples/ot_iot_device/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "esp_ot_iot_device.c"
                       INCLUDE_DIRS ".")
```

- [ ] **Step 6: 设目标并编译(用户在 PowerShell 执行)**

Run(PowerShell):
```
cd D:\code\ot\esp-thread-br\examples\ot_iot_device
idf.py set-target esp32h2
idf.py build
```
Expected: `Project build complete`。此步验证从 ot_cli 派生的骨架可编译(尚无 IoT 逻辑)。

- [ ] **Step 7: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/
git commit -m "feat(ot_iot_device): derive H2 IoT device skeleton from ot_cli"
```

---

## Task 2: EUI64 文本转换 + GPIO 开关抽象

**Files:**
- Create: `examples/ot_iot_device/main/device_eui64.h`
- Create: `examples/ot_iot_device/main/device_eui64.c`
- Create: `examples/ot_iot_device/main/device_switch.h`
- Create: `examples/ot_iot_device/main/device_switch.c`
- Modify: `examples/ot_iot_device/main/CMakeLists.txt`

**Interfaces:**
- Produces: `void device_eui64_to_string(const uint8_t in[8], char out[17]);`(与 BR 端 `eui64_to_string` 同义:16 位小写十六进制)。
- Produces: `void device_switch_init(void);`、`void device_switch_set(bool on);`、`bool device_switch_get(void);`。

- [ ] **Step 1: 写 EUI64 转换(与 BR 端约定一致)**

`examples/ot_iot_device/main/device_eui64.h`:
```c
#pragma once
#include <stdint.h>

// 8 字节 EUI64 -> 16 位小写十六进制 + '\0'(out 至少 17 字节)。首字节 -> out[0..1]。
void device_eui64_to_string(const uint8_t in[8], char out[17]);
```

`examples/ot_iot_device/main/device_eui64.c`:
```c
#include "device_eui64.h"

void device_eui64_to_string(const uint8_t in[8], char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = hexd[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hexd[in[i] & 0xf];
    }
    out[16] = '\0';
}
```

- [ ] **Step 2: 写 GPIO 开关抽象**

`examples/ot_iot_device/main/device_switch.h`:
```c
#pragma once
#include <stdbool.h>

// 初始化开关 GPIO(输出，初始关)。
void device_switch_init(void);
// 设置开关状态并驱动 GPIO。
void device_switch_set(bool on);
// 读取当前开关状态。
bool device_switch_get(void);
```

`examples/ot_iot_device/main/device_switch.c`:
```c
#include "device_switch.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#define SWITCH_GPIO CONFIG_IOT_DEVICE_SWITCH_GPIO

static bool s_state = false;

void device_switch_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SWITCH_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(SWITCH_GPIO, 0);
    s_state = false;
}

void device_switch_set(bool on) {
    s_state = on;
    gpio_set_level(SWITCH_GPIO, on ? 1 : 0);
}

bool device_switch_get(void) {
    return s_state;
}
```

- [ ] **Step 3: 添加 Kconfig(开关 GPIO + 服务名)**

创建 `examples/ot_iot_device/main/Kconfig.projbuild`:
```kconfig
menu "IoT Device Example"

    config IOT_DEVICE_SWITCH_GPIO
        int "Switch/LED GPIO number"
        default 8
        help
            控制开关状态的 GPIO(接 LED 或继电器)。ESP32-H2 devkit 板载 LED 可用 8。

    config IOT_DEVICE_SERVICE_NAME
        string "SRP service name"
        default "_iot._udp"
        help
            设备通过 SRP 注册的服务类型名。

    config IOT_DEVICE_CTRL_URI
        string "CoAP control resource path"
        default "ctrl"
        help
            接收控制命令的 CoAP Uri-Path，必须与 BR 的 MQTT_OT_BRIDGE_COAP_URI 一致。

    config IOT_DEVICE_ACK_URI
        string "BR uplink resource path"
        default "ack"
        help
            上报目标：BR 的 CoAP 资源，必须与 BR 的 MQTT_OT_BRIDGE_ACK_URI 一致。

    config IOT_DEVICE_MULTICAST_ADDR
        string "Multicast group to join"
        default "ff03::1"
        help
            加入的组播组，必须与 BR 的 MQTT_OT_BRIDGE_MULTICAST_ADDR 一致。

    config IOT_DEVICE_COAP_PORT
        int "CoAP port"
        default 5683

endmenu
```

- [ ] **Step 4: 更新 main/CMakeLists.txt**

`examples/ot_iot_device/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "esp_ot_iot_device.c"
                            "device_eui64.c"
                            "device_switch.c"
                       INCLUDE_DIRS ".")
```
**注意(实现期纠正):** `main` 组件**不要写 REQUIRES/PRIV_REQUIRES**。ot_cli 基线依赖 IDF 的
`MINIMAL_BUILD` + main 组件自动可见所有被构建组件的机制;一旦显式列出 (PRIV_)REQUIRES 就进入
严格头可见性模式,反而挡掉 `nvs_flash.h`/`esp_netif.h` 等隐式可见的头,导致 `fatal error: ...
No such file`。`driver`(GPIO)与 `openthread` 无需显式声明。

- [ ] **Step 5: 编译(用户在 PowerShell 执行)**

Run(PowerShell):
```
cd D:\code\ot\esp-thread-br\examples\ot_iot_device
idf.py build
```
Expected: `Project build complete`;menuconfig 出现 "IoT Device Example" 菜单。

- [ ] **Step 6: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/main/device_eui64.* ot_iot_device/main/device_switch.* ot_iot_device/main/Kconfig.projbuild ot_iot_device/main/CMakeLists.txt
git commit -m "feat(ot_iot_device): add EUI64 string helper and GPIO switch abstraction"
```

---

## Task 3: SRP 自动注册(用 EUI64 作 host 名)(编译 + monitor)

**Files:**
- Create: `examples/ot_iot_device/main/iot_device.c`
- Modify: `examples/ot_iot_device/main/esp_ot_iot_device.c`(移除临时 weak 桩)
- Modify: `examples/ot_iot_device/main/CMakeLists.txt`(加 iot_device.c + cjson 依赖)
- Modify: `examples/ot_iot_device/main/idf_component.yml`(加 espressif/cjson)

**Interfaces:**
- Consumes: `device_eui64_to_string`(Task 2)、`device_switch_init`(Task 2)、`otPlatRadioGetIeeeEui64`、`otSrpClientEnableAutoStartMode`、`otSrpClientSetHostName`、`otSrpClientEnableAutoHostAddress`、`otSrpClientBuffers*`、`otSrpClientAddService`。
- Produces: 真实 `iot_device_start()`;`static char s_eui64_str[17];`(设备身份,Task 4/5 复用);`static void srp_register(otInstance *inst);`。

- [ ] **Step 1: 移除临时桩**

从 `esp_ot_iot_device.c` 末尾删除:
```c
// 临时桩：Task 3 由 iot_device.c 提供真实实现(强符号覆盖)。
__attribute__((weak)) void iot_device_start(void) {}
```

- [ ] **Step 2: 写 iot_device.c 的 SRP 注册部分**

`examples/ot_iot_device/main/iot_device.c`:
```c
#include "iot_device.h"
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "device_eui64.h"
#include "device_switch.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "openthread/platform/radio.h"

#define TAG "iot_device"

static char s_eui64_str[17];

static void srp_autostart_cb(const otSockAddr *server, void *ctx) {
    (void)ctx;
    ESP_LOGI(TAG, "SRP auto-start: server found, host=%s", s_eui64_str);
}

// 调用者必须已持 OT 锁。
static void srp_register(otInstance *inst) {
    // host 名 = EUI64 文本
    uint8_t eui[8];
    otPlatRadioGetIeeeEui64(inst, eui);
    device_eui64_to_string(eui, s_eui64_str);

    uint16_t size = 0;
    char *host_name_buf = otSrpClientBuffersGetHostNameString(inst, &size);
    strncpy(host_name_buf, s_eui64_str, size - 1);
    host_name_buf[size - 1] = '\0';
    otSrpClientSetHostName(inst, host_name_buf);
    otSrpClientEnableAutoHostAddress(inst);

    // 服务：instance name = EUI64，service name = CONFIG_IOT_DEVICE_SERVICE_NAME
    otSrpClientBuffersServiceEntry *entry = otSrpClientBuffersAllocateService(inst);
    if (entry == NULL) {
        ESP_LOGE(TAG, "SRP buffer alloc failed");
        return;
    }
    char *inst_name = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
    strncpy(inst_name, s_eui64_str, size - 1);
    inst_name[size - 1] = '\0';

    char *svc_name = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
    strncpy(svc_name, CONFIG_IOT_DEVICE_SERVICE_NAME, size - 1);
    svc_name[size - 1] = '\0';

    entry->mService.mPort = CONFIG_IOT_DEVICE_COAP_PORT;

    otError err = otSrpClientAddService(inst, &entry->mService);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "SRP add service err=%d", err);
        return;
    }
    otSrpClientEnableAutoStartMode(inst, srp_autostart_cb, NULL);
    ESP_LOGI(TAG, "SRP registration queued: host/instance=%s service=%s",
             s_eui64_str, CONFIG_IOT_DEVICE_SERVICE_NAME);
}

void iot_device_start(void) {
    device_switch_init();

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    srp_register(inst);
    esp_openthread_lock_release();
}
```
注:`portMAX_DELAY` 需要 FreeRTOS 头;顶部追加 `#include "freertos/FreeRTOS.h"` 与 `#include "freertos/task.h"`。

- [ ] **Step 3: 更新 CMakeLists + idf_component.yml**

`examples/ot_iot_device/main/CMakeLists.txt`(加 iot_device.c;**仍不写 REQUIRES**,cjson 经 idf_component.yml 声明后 main 自动可见):
```cmake
idf_component_register(SRCS "esp_ot_iot_device.c"
                            "device_eui64.c"
                            "device_switch.c"
                            "iot_device.c"
                       INCLUDE_DIRS ".")
```

`examples/ot_iot_device/main/idf_component.yml` 在 `dependencies:` 下追加(保留 ot_cli 原有项):
```yaml
  espressif/cjson: "^1.7.19"
```

- [ ] **Step 4: 编译(用户在 PowerShell 执行)**

Run(PowerShell):
```
cd D:\code\ot\esp-thread-br\examples\ot_iot_device
idf.py build
```
Expected: `Project build complete`(首次拉取 espressif/cjson)。

- [ ] **Step 5: monitor 验证(需 H2 硬件 + 已组网的 BR)**

Run(PowerShell): `idf.py -p COM<x> flash monitor`
Expected:入网后日志 `SRP registration queued: host/instance=<eui64> service=_iot._udp`,连上 BR 后 `SRP auto-start: server found`。在 BR 端 monitor 应看到 `published registry: 1 devices`,broker `otbr/dev/registry` 出现该 EUI64。

- [ ] **Step 6: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/main/
git commit -m "feat(ot_iot_device): SRP auto-registration using factory EUI64 as host name"
```

---

## Task 4: CoAP server(ctrl 资源)+ 开关命令执行(编译 + monitor)

**Files:**
- Modify: `examples/ot_iot_device/main/iot_device.c`

**Interfaces:**
- Consumes: `device_switch_set`/`device_switch_get`(Task 2)、`s_eui64_str`(Task 3)、`otCoapStart`、`otCoapAddResource`、`otMessageGetOffset`/`otMessageGetLength`/`otMessageRead`、cJSON。
- Produces: `static void ctrl_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info);`、`static otCoapResource s_ctrl_resource;`、`static void device_report(const char *reqid, const otMessageInfo *dst_hint);`(Task 5 用于上报;本任务先声明并在命令处理末尾调用)。
- Produces: `static bool parse_command(const char *json, char *reqid_out, size_t reqid_cap, bool *on_out, bool *is_query_out);`(cJSON 解析:识别 `{"reqid":..,"cmd":"on|off|query"}`)。

- [ ] **Step 1: 追加 CoAP 头 + payload 读取 + cJSON**

在 iot_device.c 顶部包含区追加:
```c
#include <stdlib.h>
#include "cJSON.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"
```

在文件中加入 payload 读取工具:
```c
#define COAP_PAYLOAD_MAX 512

static int coap_read_payload(otMessage *msg, char *buf, int buf_size) {
    uint16_t offset = otMessageGetOffset(msg);
    uint16_t total  = otMessageGetLength(msg);
    int len = (int)total - (int)offset;
    if (len < 0) {
        len = 0;
    }
    if (len > buf_size - 1) {
        len = buf_size - 1;
    }
    int read = otMessageRead(msg, offset, buf, len);
    buf[read] = '\0';
    return read;
}
```

- [ ] **Step 2: 写命令解析(cJSON)**

```c
// 解析 {"reqid":"..","cmd":"on|off|query"}。识别成功返回 true。
static bool parse_command(const char *json, char *reqid_out, size_t reqid_cap,
                          bool *on_out, bool *is_query_out) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    reqid_out[0] = '\0';
    *is_query_out = false;
    *on_out = false;

    cJSON *reqid = cJSON_GetObjectItem(root, "reqid");
    if (cJSON_IsString(reqid) && reqid->valuestring != NULL) {
        strncpy(reqid_out, reqid->valuestring, reqid_cap - 1);
        reqid_out[reqid_cap - 1] = '\0';
    }
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
        if (strcmp(cmd->valuestring, "on") == 0) {
            *on_out = true; ok = true;
        } else if (strcmp(cmd->valuestring, "off") == 0) {
            *on_out = false; ok = true;
        } else if (strcmp(cmd->valuestring, "query") == 0) {
            *is_query_out = true; ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}
```

- [ ] **Step 3: 写 ctrl 资源 handler(执行 + 触发上报)**

Task 5 将实现 `device_report`;此处先声明原型并在末尾调用:
```c
static void device_report(const char *reqid);   // Task 5 实现

static void ctrl_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx; (void)info;
    char payload[COAP_PAYLOAD_MAX];
    coap_read_payload(msg, payload, sizeof(payload));

    char reqid[64];
    bool on = false, is_query = false;
    if (!parse_command(payload, reqid, sizeof(reqid), &on, &is_query)) {
        ESP_LOGW(TAG, "ctrl: bad command payload");
        return;
    }
    if (!is_query) {
        device_switch_set(on);
        ESP_LOGI(TAG, "ctrl: set switch=%d reqid=%s", on, reqid);
    } else {
        ESP_LOGI(TAG, "ctrl: query reqid=%s", reqid);
    }
    // 组播命令(NON)与单播命令(CON)都统一以"单播上报到 BR 的 ack"回执。
    device_report(reqid);
}

static otCoapResource s_ctrl_resource = {
    .mUriPath = CONFIG_IOT_DEVICE_CTRL_URI,
    .mHandler = ctrl_request_handler,
    .mContext = NULL,
    .mNext = NULL,
};
```

- [ ] **Step 4: 在 iot_device_start 里启动 CoAP + 注册资源**

修改 `iot_device_start()`,在持锁块内、`srp_register` 之后追加:
```c
    otError coap_err = otCoapStart(inst, CONFIG_IOT_DEVICE_COAP_PORT);
    if (coap_err == OT_ERROR_NONE) {
        otCoapAddResource(inst, &s_ctrl_resource);
        ESP_LOGI(TAG, "CoAP started, resource '%s' registered", CONFIG_IOT_DEVICE_CTRL_URI);
    } else {
        ESP_LOGE(TAG, "CoAP start err=%d", coap_err);
    }
```

- [ ] **Step 5: 编译(用户在 PowerShell 执行)**

Run(PowerShell): `cd D:\code\ot\esp-thread-br\examples\ot_iot_device && idf.py build`
Expected: `Project build complete`。注意:`device_report` 此时仅有原型,若链接报未定义,说明 Task 5 未完成——本任务与 Task 5 相邻,建议连续实现后再编译;或先加临时空桩 `static void device_report(const char *reqid){(void)reqid;}` 供独立编译,Task 5 替换。

- [ ] **Step 6: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/main/iot_device.c
git commit -m "feat(ot_iot_device): CoAP ctrl resource parses command and drives switch"
```

---

## Task 5: 单播上报到 BR 的 ack 资源 + 组播加组 + 抖动(编译 + monitor)

**Files:**
- Modify: `examples/ot_iot_device/main/iot_device.c`

**Interfaces:**
- Consumes: `s_eui64_str`(Task 3)、`device_switch_get`(Task 2)、`otCoapNewMessage`/`otCoapMessageInit`/`otCoapMessageAppendUriPathOptions`/`otCoapMessageSetPayloadMarker`/`otMessageAppend`/`otCoapSendRequest`/`otMessageFree`、`otIp6SubscribeMulticastAddress`、`otIp6AddressFromString`、cJSON、`esp_random`。
- Produces: 真实 `device_report(const char *reqid)`(替换 Task 4 的原型/桩)、`static otIp6Address s_br_addr; static bool s_br_addr_valid;`(从 SRP server 地址获取 BR 地址)、组播加组逻辑。

**说明:** 上报要发到"BR 的地址"。BR 即设备的 SRP server,`otSrpClientGetServerAddress` 可取其地址。上报用 CoAP NON(单向通知,不需要 BR 回 ACK),POST 到 `ack` 资源。

- [ ] **Step 1: 确认取 SRP server 地址的 API 可用**

在 iot_device.c 顶部包含区已含 `openthread/srp_client.h`。使用 `otSrpClientGetServerAddress(otInstance*)` 返回 `const otSockAddr *`,其 `mAddress` 即 BR 的 IPv6。

- [ ] **Step 2: 写 device_report(单播 NON POST 到 ack)**

替换 Task 4 的 `device_report` 原型/桩为实现:
```c
// 构造状态 JSON 并单播上报到 BR 的 ack 资源。调用者在 OT 任务上下文(CoAP 回调)中，已持锁。
static void device_report(const char *reqid) {
    otInstance *inst = esp_openthread_get_instance();

    // 取 BR(SRP server)地址作为上报目标
    const otSockAddr *server = otSrpClientGetServerAddress(inst);
    if (server == NULL) {
        ESP_LOGW(TAG, "report: no SRP server address yet");
        return;
    }

    // 构造 JSON: {"id":<eui64>,"reqid":<reqid>,"state":"on|off"}
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "id", s_eui64_str);
    cJSON_AddStringToObject(root, "reqid", reqid);
    cJSON_AddStringToObject(root, "state", device_switch_get() ? "on" : "off");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return;
    }

    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) {
        free(json);
        return;
    }
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_IOT_DEVICE_ACK_URI);
    otCoapMessageSetPayloadMarker(msg);
    otMessageAppend(msg, json, (uint16_t)strlen(json));

    otMessageInfo info;
    memset(&info, 0, sizeof(info));
    info.mPeerAddr = server->mAddress;
    info.mPeerPort = CONFIG_IOT_DEVICE_COAP_PORT;

    otError err = otCoapSendRequest(inst, msg, &info, NULL, NULL);
    if (err != OT_ERROR_NONE) {
        otMessageFree(msg);
        ESP_LOGE(TAG, "report send err=%d", err);
    } else {
        ESP_LOGI(TAG, "reported state=%s reqid=%s", device_switch_get() ? "on" : "off", reqid);
    }
    free(json);
}
```
顶部包含区追加 `#include "esp_random.h"`(Step 4 抖动用)。

- [ ] **Step 3: 加入组播组**

在 `iot_device_start()` 持锁块内、CoAP 启动之后追加:
```c
    otIp6Address maddr;
    if (otIp6AddressFromString(CONFIG_IOT_DEVICE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
        otError merr = otIp6SubscribeMulticastAddress(inst, &maddr);
        ESP_LOGI(TAG, "subscribe multicast %s err=%d", CONFIG_IOT_DEVICE_MULTICAST_ADDR, merr);
    } else {
        ESP_LOGE(TAG, "bad multicast addr");
    }
```

- [ ] **Step 4: 组播命令的抖动**

在 `ctrl_request_handler` 中,判断是否为组播命令(目标是多播地址),若是则在上报前延迟随机 0~500ms。修改 handler,把 `device_report(reqid)` 调用替换为:
```c
    // 组播命令(本地收包地址为多播，IPv6 多播首字节为 0xff)执行后随机抖动 0~500ms
    // 再上报，避免响应风暴。
    bool via_multicast = (info->mSockAddr.mFields.m8[0] == 0xff);
    if (via_multicast) {
        uint32_t jitter_ms = esp_random() % 501;
        ESP_LOGI(TAG, "multicast cmd, jitter %u ms", (unsigned)jitter_ms);
        vTaskDelay(pdMS_TO_TICKS(jitter_ms));
    }
    device_report(reqid);
```
注:OpenThread 无 `otIp6IsMulticastAddress` 函数;IPv6 多播地址首字节固定为 `0xff`,故用
`info->mSockAddr.mFields.m8[0] == 0xff` 判断。`info->mSockAddr` 是本地收包地址,组播命令时为多播组地址。
`vTaskDelay`/`pdMS_TO_TICKS` 需 FreeRTOS 头(已在 Task 3 引入)。

**⚠️ 时序注意:** `ctrl_request_handler` 运行在 OT 任务上下文且已持锁;在其中 `vTaskDelay` 会阻塞 OT 任务。为避免阻塞协议栈,组播抖动应改为"记录待上报 + 定时器/独立任务延迟发送",而非在回调里直接 delay。实现时若发现阻塞问题,改用 `xTimerCreate` 单次定时器承载 `device_report`。本步先按简单 delay 实现,monitor 验证时观察是否影响入网稳定性。

- [ ] **Step 5: 编译(用户在 PowerShell 执行)**

Run(PowerShell): `cd D:\code\ot\esp-thread-br\examples\ot_iot_device && idf.py build`
Expected: `Project build complete`。

- [ ] **Step 6: monitor 端到端验证(需 H2 + BR + broker)**

Run(PowerShell): `idf.py -p COM<x> flash monitor`
向 broker publish 验证:
- 单播:`otbr/cmd/unicast/<eui64>` payload `{"reqid":"u1","cmd":"on"}` → 设备日志 `ctrl: set switch=1 reqid=u1` + `reported state=on reqid=u1`;broker `otbr/dev/response` 收到 `{"id":<eui64>,"reqid":"u1","state":"on"}`;LED 点亮。
- 组播:`otbr/cmd/multicast` payload `{"reqid":"m1","cmd":"off"}` → 设备 `multicast cmd, jitter N ms` + 上报;broker `dev/response` 收到对应 reqid。
- 查询:`{"reqid":"q1","cmd":"query"}` → 不改状态,回报当前 state。

- [ ] **Step 7: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/main/iot_device.c
git commit -m "feat(ot_iot_device): unicast state report to BR ack, multicast join + jitter"
```

---

## Task 6: README + 文档(与 BR 契约对齐)

**Files:**
- Create: `examples/ot_iot_device/README.md`

- [ ] **Step 1: 写 README**

`examples/ot_iot_device/README.md`:
```markdown
# ot_iot_device

ESP32-H2 上的示例 IoT 开关设备，作为 `common/mqtt_ot_bridge` 的受控端。
从 `ot_cli` 派生，保留 OpenThread CLI 便于调试，额外提供：SRP 自动注册、
CoAP `ctrl` 资源接收开关命令、执行后单播上报到 BR 的 `ack` 资源。

## 编译烧录（ESP-IDF PowerShell）
    idf.py set-target esp32h2
    idf.py build
    idf.py -p COM<x> flash monitor

## 配置（menuconfig → IoT Device Example）
- `IOT_DEVICE_SWITCH_GPIO`：开关/LED 的 GPIO（H2 devkit 板载 LED 默认 8）
- `IOT_DEVICE_SERVICE_NAME`：SRP 服务名（默认 `_iot._udp`）
- `IOT_DEVICE_CTRL_URI` / `IOT_DEVICE_ACK_URI`：与 BR 的对应 Kconfig 必须一致
- `IOT_DEVICE_MULTICAST_ADDR`：组播组（默认 `ff03::1`，须与 BR 一致）

## 身份与契约
- 设备身份 = 出厂 EUI64（`otPlatRadioGetIeeeEui64`），16 位小写十六进制。
- SRP host name 与 service instance name 均为该 EUI64，BR 靠 host 名前 16 字符匹配。
- MQTT 侧用 `otbr/cmd/unicast/<eui64>` 单播控制、`otbr/cmd/multicast` 组播控制。

## 命令 payload（服务端定义，设备用 cJSON 解析）
| 字段 | 说明 |
|------|------|
| `reqid` | 服务端生成的唯一标识，设备原样回带用于对账 |
| `cmd` | `on` / `off` / `query` |

上报 payload：`{"id":<eui64>,"reqid":<reqid>,"state":"on|off"}`，
经 BR 透传到 `otbr/dev/response`。

## 端到端联调
1. 先让 BR（basic_thread_border_router，启用 mqtt_ot_bridge）组网并连上 broker。
2. 本设备上电入网 → SRP 注册 → BR 的 `dev/registry` 出现本设备 EUI64。
3. 向 `otbr/cmd/unicast/<eui64>` 或 `otbr/cmd/multicast` 发命令，观察 LED 与 `dev/response`。
```

- [ ] **Step 2: 提交**

```bash
cd /d/code/ot/esp-thread-br/examples
git add ot_iot_device/README.md
git commit -m "docs(ot_iot_device): usage and BR contract"
```

---

## Self-Review Notes

- **契约一致性**:设备端 `ctrl`/`ack`/`ff03::1`/EUI64 host 名/reqid 回带,与 `common/mqtt_ot_bridge/README.md` 及 BR 端 Kconfig 默认值逐项对齐。
- **API 核实**:`otSrpClientEnableAutoStartMode`/`otSrpClientBuffers*`/`otSrpClientAddService`/`otSrpClientGetServerAddress`、`otCoapStart`/`otCoapAddResource`/`otCoapSendRequest`、`otIp6SubscribeMulticastAddress`/`otIp6AddressFromString`、`otPlatRadioGetIeeeEui64` 均在 IDF v6.0.2 头文件确认存在。组播判断:OpenThread **无** `otIp6IsMulticastAddress`,改用 IPv6 多播首字节 `0xff` 检测(`mFields.m8[0]`)。
- **待硬件确认的风险**:(1) 组播抖动在 CoAP 回调里 `vTaskDelay` 可能阻塞 OT 任务 → 已标注,必要时改定时器承载 device_report;(2) `otSrpClientGetServerAddress` 在 auto-start 未完成时可能无效 → device_report 已判空;(3) `info->mSockAddr` 是否可靠反映组播目标 → monitor 验证。
- **占位符扫描**:无 TBD;每步含完整代码。硬件步骤给出明确 publish/日志预期。
- **依赖**:cjson 用 `espressif__cjson`(v6 托管组件),与 BR 端一致。
```
