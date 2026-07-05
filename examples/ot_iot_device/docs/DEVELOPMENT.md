# ot_iot_device 开发手册：如何扩展 H2 设备功能

本手册面向想在 `ot_iot_device` 基础上开发新 Thread IoT 设备的工程师。假设你熟悉 C
和基本嵌入式开发，但不了解本工程的结构与 OpenThread/CoAP 约定。

> 配套阅读：`README.md`（使用与联调）、`common/mqtt_ot_bridge/README.md`（BR 端契约）。
> 所有 `idf.py` 命令必须在 **ESP-IDF PowerShell/CMD** 运行，不能在 Git Bash。

---

## 1. 先建立心智模型

设备是 `mqtt_ot_bridge`（BR）的受控端。一条命令的完整生命周期：

```
  服务端                 BR (C6)                   H2 设备 (本工程)
  ──────                ────────                  ────────────────
  MQTT publish   ─→  cmd/unicast/<eui64>  ─CoAP NON─→  ctrl_request_handler()
  {"reqid","cmd"}    cmd/multicast        (ff03::1)         │
                                                    parse_command()  ← 解析 JSON
                                                            │
                                                    device_switch_set()  ← 执行动作
                                                            │
                                                    定时器 → device_report()
  dev/response   ←─  /ack 资源透传  ←──CoAP NON单播──  {"id","reqid","state"}
```

**三条不可违背的规则**（否则和 BR 对不上）：

1. **设备身份 = EUI64**。SRP host/instance 名都是 16 位小写十六进制 EUI64。BR 靠它路由和匹配。
2. **回执走 `/ack` + reqid**。设备执行后单播上报到 BR 的 `ack` 资源，原样带回服务端下发的 `reqid`。不要用 CoAP CON 的 ACK 做回执（BR 单播是 NON）。
3. **OT API 必须持锁**。CoAP 回调里已持锁（勿再 acquire）；自建任务/定时器里要自己 `esp_openthread_lock_acquire/release`。

---

## 2. 代码地图（`main/` 下）

| 文件 | 职责 | 你扩展时是否常改 |
|------|------|------------------|
| `esp_ot_iot_device.c` | `app_main`：OT 启动骨架 + 调 `iot_device_start()` | 很少 |
| `iot_device.c` | 核心：SRP 注册、CoAP `ctrl` 资源、命令解析、状态上报 | **经常** |
| `device_switch.c/.h` | GPIO 开关抽象（`init/set/get`） | 换硬件动作时改 |
| `device_eui64.c/.h` | EUI64 字节→文本（与 BR 端约定一致） | 几乎不改 |
| `Kconfig.projbuild` | 配置项（GPIO、服务名、资源路径、组播地址、端口） | 加配置时改 |

`iot_device.c` 内部分区（按注释分隔）：
- **SRP 自动注册** — `srp_register()`，用 EUI64 作 host/instance 名
- **状态上报** — `device_report()`（构造 JSON + 单播到 `/ack`）、`report_timer_cb()`（定时器承载抖动）
- **CoAP ctrl 资源** — `coap_read_payload()`、`parse_command()`、`ctrl_request_handler()`
- **启动** — `iot_device_start()`（初始化开关 + 建定时器 + 注册 SRP + 启 CoAP + 加组播组）

---

## 3. 扩展场景 A：增加一个新命令（最常见）

**目标**：除了 `on/off/query`，再加一个 `toggle`（翻转）命令。

只需改 `iot_device.c` 两处。

**① 扩展 `parse_command()`**（约 `iot_device.c:180`）——增加对 `toggle` 的识别。
建议把"命令类型"从 bool 改成枚举，扩展性更好。最小改动版本先加一个输出参数：

```c
// 在 parse_command 的 cmd 分支里，追加一个 toggle 分支：
} else if (strcmp(cmd->valuestring, "toggle") == 0) {
    *toggle_out = true; ok = true;   // toggle_out 是你新增的输出参数
}
```

**② 在 `ctrl_request_handler()` 里处理**（约 `iot_device.c:206`）：

```c
if (toggle) {
    device_switch_set(!device_switch_get());   // 翻转
    ESP_LOGI(TAG, "ctrl: toggle -> %d reqid=%s", device_switch_get(), reqid);
} else if (!is_query) {
    device_switch_set(on);
    ...
}
```

上报逻辑不用动——`device_report()` 会自动带上当前 `state`。

**验证**：`idf.py build` → flash → 向 `otbr/cmd/unicast/<eui64>` 发 `{"reqid":"x","cmd":"toggle"}`，
看 LED 翻转 + `dev/response` 回报新状态。

> **提示**：如果命令种类越来越多，建议把 `bool on/is_query/toggle` 重构成
> `typedef enum { CMD_ON, CMD_OFF, CMD_QUERY, CMD_TOGGLE } cmd_kind_t;`，
> `parse_command` 返回枚举，`ctrl_request_handler` 用 switch 分派。

---

## 4. 扩展场景 B：换一种设备（如调光灯、传感器）

设备的"动作"被隔离在 `device_switch.c` 里，换硬件主要改这里 + payload 字段。

### B1. 调光灯（带亮度）

**① 硬件抽象**：把 `device_switch.c` 换成 PWM 驱动，或新建 `device_dimmer.c/.h`：

```c
// device_dimmer.h
void device_dimmer_init(void);
void device_dimmer_set(uint8_t level);   // 0~100
uint8_t device_dimmer_get(void);
```
实现用 LEDC（ESP-IDF PWM 外设，`driver/ledc.h`）。

**② 命令 payload 加 `level` 字段**。在 `parse_command()` 里解析：

```c
cJSON *level = cJSON_GetObjectItem(root, "level");
if (cJSON_IsNumber(level)) {
    *level_out = (uint8_t)level->valueint;
}
```

**③ 上报 payload 加亮度**。在 `device_report()` 构造 JSON 处（约 `iot_device.c:100`）：

```c
cJSON_AddNumberToObject(root, "level", device_dimmer_get());
```

命令示例：`{"reqid":"d1","cmd":"on","level":60}`
上报示例：`{"id":<eui64>,"reqid":"d1","state":"on","level":60}`

### B2. 传感器（只上报，不受控）

传感器不接收命令，而是**周期主动上报**。复用现有的定时器模式：

1. 保留 SRP 注册（这样 BR 的 `dev/registry` 能发现它），服务名改为 `_sensor._udp`
   （`Kconfig` 的 `IOT_DEVICE_SERVICE_NAME`）。
2. 不注册 `ctrl` 资源（或保留一个 `query`）。
3. 新建一个**周期性** FreeRTOS 定时器（`pdTRUE` 自动重载），回调里读传感器 +
   调 `device_report()` 的变体（上报 `{"id","temp":25.3,"humi":60}`）。

> 注意：`device_report()` 目前上报到 BR 的 `ack` 资源，reqid 用命令带的。
> 传感器主动上报没有 reqid，可传空串或时间戳，BR 一样透传到 `dev/response`。

---

## 5. 扩展场景 C：设备用 SRP TXT 记录声明元数据

BR 的 `dev/registry` 目前上报 `{eui64, ipv6, service}`。如果想让服务端知道更多
（如设备型号、能力、友好名），可以通过 **SRP TXT 记录**携带——BR 端可扩展读取。

**设备端**：在 `srp_register()`（约 `iot_device.c:47`）里，`otSrpClientBuffersAllocateService`
之后填 TXT。用 `otSrpClientBuffersGetServiceEntryTxtBuffer` 取缓冲，构造 `otDnsTxtEntry`：

```c
// 伪代码：设置 TXT "model=dimmer-v1"
uint16_t txt_size;
uint8_t *txt_buf = otSrpClientBuffersGetServiceEntryTxtBuffer(entry, &txt_size);
// 按 DNS-SD TXT 格式填 key=value，设置 entry->mTxtEntry / mService.mTxtEntries
```

> 这需要 BR 端配合扩展 `registry_collect()` 去读 `otSrpServerServiceGetTxtData`。
> 属于**跨两端的改动**，改前建议先在 OpenSpec 里更新契约。TXT 格式细节见
> OpenThread `otSrpClientBuffers*` 与 `otDnsTxtEntry` 文档。

---

## 6. 关键约束与陷阱（来自真实联调）

这些是本项目踩过的坑，扩展时务必遵守：

### 6.1 任务栈很小 —— 别在回调/定时器里放大数组
BR 端曾因在定时器任务（栈 ~2KB）里放 4KB 栈数组而崩溃（Stack protection fault）。
设备端同理：`ctrl_request_handler` 在 OT 任务上下文（栈较大，512B payload 可以），
但**定时器回调里**若要大缓冲，用 `malloc/calloc` 堆分配，别用大局部数组。

### 6.2 别在 CoAP 回调里 `vTaskDelay`
`ctrl_request_handler` 运行在 OpenThread 任务上下文。在里面 `vTaskDelay` 会**阻塞整个
协议栈**。需要延迟（如组播抖动）时，用 FreeRTOS 单次定时器承载（见现有 `s_report_timer`
的用法，`iot_device.c:221`）。

### 6.3 OT 锁的边界
- CoAP 资源回调（`ctrl_request_handler`）、SRP 回调：**已持锁**，直接调 OT API，勿 acquire。
- 你新建的任务/定时器回调（如 `report_timer_cb`）：**未持锁**，必须
  `esp_openthread_lock_acquire(portMAX_DELAY)` … `release()` 包住 OT API 调用。

### 6.4 回执用 NON，不要指望 CON 的 ACK
BR 下行单播/组播都是 CoAP **NON**。设备的回执机制是"单播上报到 `/ack`"，不是回 CoAP ACK。
如果你新增设备间通信用了 CON，记得对端要 `otCoapSendResponse` 回 ACK，否则会重传。

### 6.5 payload 大小
`COAP_PAYLOAD_MAX = 512`（`iot_device.c:26`）。命令或上报 JSON 超过这个会被截断。
字段多时调大这个宏，并注意 CoAP/6LoWPAN 分片对大包的开销。

---

## 7. 新增一个 Kconfig 配置项

以调光灯的 PWM GPIO 为例：

**① 在 `main/Kconfig.projbuild` 的 `menu` 内加**：
```kconfig
    config IOT_DEVICE_PWM_GPIO
        int "Dimmer PWM GPIO"
        default 10
```

**② 代码里用** `CONFIG_IOT_DEVICE_PWM_GPIO`（需 `#include "sdkconfig.h"`）。

**③** `idf.py menuconfig` → "IoT Device Example" 菜单可见并可改；或加到 `sdkconfig.defaults`。

---

## 8. 扩展后的验证清单

每次扩展，按这个顺序验证（本机无 host 测试，全靠编译+monitor）：

1. **编译**（PowerShell）：`idf.py build` → `Project build complete`。
2. **单机 monitor**：`idf.py -p COM<x> flash monitor`，确认启动日志链：
   ```
   SRP registration queued: host/instance=<eui64> service=<你的服务名>
   CoAP started, resource 'ctrl' registered
   subscribe multicast ff03::1 err=0
   SRP auto-start: server found
   ```
3. **BR 侧确认注册**：BR 的 `dev/registry`（retained）出现本设备 EUI64。
   或在 BR 的 CLI 敲 `ot srp server host` / `ot srp server service` 查看。
4. **命令闭环**：向 `otbr/cmd/unicast/<eui64>` 发新命令，看设备日志 + LED/动作 +
   `otbr/dev/response` 的回执 payload。
5. **组播**（如涉及）：`otbr/cmd/multicast` 发命令，多台设备应各自抖动后回执，
   `dev/response` 收到多条不同 EUI64 的响应。

---

## 9. 常见问题排查

| 现象 | 可能原因 | 查法 |
|------|----------|------|
| `dev/registry` 里没有本设备 | 设备没跑完 SRP 注册 / host 名不是 EUI64 开头 | BR CLI `ot srp server host`；设备日志有无 `SRP auto-start` |
| 发命令设备无反应 | topic 里 EUI64 错、设备没订阅组播、`ctrl` 资源路径不一致 | 设备日志有无 `ctrl:` 行；核对 BR/设备的 `CTRL_URI` 一致 |
| `dev/response` 收到多条相同 reqid | 用了 CON 但没回 ACK 导致重传 | 确认下行是 NON；设备端别对 CON 不回 ACK |
| 启动即崩 Stack protection fault | 回调/定时器里放了大栈数组 | 看 backtrace 的函数；改堆分配 |
| 设备重启后 EUI64 变了 | 不会——EUI64 来自出厂 efuse，`otPlatRadioGetIeeeEui64` | — |

---

## 10. 更大的改动：先更新契约

如果扩展会改变**BR ↔ 设备之间的约定**（新的 topic、新的 payload 字段语义、
TXT 记录、新的资源路径），这不再是纯设备端改动。建议：

1. 先在 OpenSpec change `add-mqtt-iot-bridge` 的 spec 里更新契约（`specs/coap-device-control`
   或 `specs/device-registry`）。
2. BR 端（`common/mqtt_ot_bridge`）与设备端（`ot_iot_device`）同步改。
3. 两端 `README.md` 的契约表同步更新。

保持两端契约文档一致，是这个系统可维护的关键——BR 是无状态透传管道，
**所有语义都是两端约定出来的，没有中心真相**。
