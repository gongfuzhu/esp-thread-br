# MQTT ↔ Thread IoT 系统迭代手册

本手册面向**下一次迭代**这套系统的人:如何在既有基础上安全地增加/修改功能,
走通"规划 → 实现 → 验证 → 归档"的完整流程。它是顶层向导,细节指向各子文档。

> 这是**流程手册**,不是使用说明。第一次上手请先读各组件的 README。

---

## 1. 这套系统是什么

一个把局域网 MQTT 命令翻译成 Thread 网内 CoAP 控制的系统,由两端组成:

```
   服务端(手机/PC)          BR 主机固件 (ESP32-C6)         Thread 设备 (ESP32-H2)
   ──────────────          ─────────────────────         ──────────────────────
   MQTT broker    ◀──────▶  common/mqtt_ot_bridge  ◀────▶  ot_iot_device
   (登陆/发命令/收状态)      无状态 MQTT↔CoAP 管道          SRP注册+CoAP server+开关
```

**一句话架构:BR 是无状态透传管道。** 它不理解 payload 语义、不对账、不分组、
不生成 ID。所有语义都是**服务端和设备两端约定**出来的——BR 只搬运字节。
这是理解整个系统的钥匙,也是迭代时最容易违背的原则。

完整设计与推理见归档的 OpenSpec change:
`openspec/changes/archive/2026-07-05-add-mqtt-iot-bridge/`(proposal / design / specs)。

---

## 2. 文档地图(先知道去哪查)

| 你想做的事 | 看这个 |
|-----------|--------|
| 了解 BR 端配置与 topic 约定 | `common/mqtt_ot_bridge/README.md` |
| 了解设备端使用与联调 | `ot_iot_device/README.md` |
| **扩展设备功能**(加命令/换设备类型) | `ot_iot_device/docs/DEVELOPMENT.md` |
| 系统正式契约(需求+场景) | `openspec/specs/{mqtt-bridge,coap-device-control,device-registry}/spec.md` |
| 当初怎么设计的、为什么 | 归档 change 的 `design.md` |
| 参考实现的任务拆解方式 | 归档 change 的 `tasks.md`、`docs/superpowers/plans/*.md` |
| **本手册** | 顶层迭代流程 |

---

## 3. 工具链现实(每次迭代都要面对)

这台机器的 ESP-IDF 环境有几个硬约束,踩过多次,写死在这:

- **IDF 版本 v6.0.2**,安装在 `D:\esp\v6.0.2\esp-idf`。
- **`idf.py` 必须在 ESP-IDF PowerShell/CMD 里跑**,**不能在 Git Bash**
  (会报 `MSys/Mingw is no longer supported`)。
- **本机没有 host gcc/make**,纯逻辑模块的 host 单测跑不了;验证靠**编译 + 设备 monitor**。
- **esp-mqtt / cJSON 是 IDF v6 托管组件**:依赖声明用 `espressif/mqtt`、`espressif/cjson`;
  CMake REQUIRES 用双下划线 `espressif__mqtt`、`espressif__cjson`。
- **派生自 ot_cli 的工程(如 ot_iot_device),main 组件不要写 REQUIRES** —— 会触发严格
  头可见模式,反而挡掉 `nvs_flash.h` 等隐式可见的头。

编译/烧录基本命令(PowerShell):
```
cd D:\code\ot\esp-thread-br\examples\<工程目录>
idf.py set-target <esp32c6 | esp32h2>   # 仅首次或换目标
idf.py build
idf.py -p COM<x> flash monitor
```

---

## 4. 迭代类型 → 走哪条流程

改动的**影响范围**决定流程重量。先判断你的改动属于哪类:

```
   ┌─ 仅设备端行为(加命令、换硬件动作)        → 流程 A(轻)
   │    不改 BR、不改 topic/payload 语义
   │
   ├─ 仅 BR 端行为(日志、内部实现、性能)       → 流程 A(轻)
   │    不改对外契约
   │
   └─ 改变两端契约                              → 流程 B(重,契约驱动)
        新 topic / 新 payload 字段语义 /
        新 CoAP 资源 / SRP TXT / registry 结构
```

**判断口诀:** 如果你的改动会让"服务端或另一端必须跟着改才能对接",那就是契约变更 → 流程 B。

---

## 5. 流程 A:单端改动(轻量)

适用于不改变两端约定的改动。以"给设备加 `toggle` 命令"为例:

1. **改代码**。设备端扩展见 `ot_iot_device/docs/DEVELOPMENT.md` 的场景章节
   (含具体改哪个函数、哪一行)。
2. **编译**(PowerShell):`idf.py build` → `Project build complete`。
3. **单机 monitor 验证**:烧录,看启动日志链正常。
4. **端到端验证**:通过 broker 发命令,观察设备动作 + `dev/response` 回执。
5. **更新对应 README**(如新增了命令/配置项)。
6. **提交**:`feat(...)` / `fix(...)`,信息说明改了什么、验证方式。

> 单端改动不必动 OpenSpec——契约没变。

---

## 6. 流程 B:契约变更(契约驱动,重量)

适用于改变 BR↔设备约定的改动。**必须先更新契约,再改两端,最后归档。**
以"设备通过 SRP TXT 上报型号,registry 增加 model 字段"为例:

### 6.1 开一个新的 OpenSpec change
```
openspec change new <change-name>          # 如 add-device-model-metadata
```
按提示写 proposal / design / specs(delta)/ tasks。参考已归档的
`add-mqtt-iot-bridge` 的写法。

- 契约变更落在哪个 spec:
  - topic/登陆/透传 → `mqtt-bridge`
  - CoAP 单播/组播/`/ack`/资源 → `coap-device-control`
  - SRP 发现/registry/选路 → `device-registry`
- 用 delta 头(`## ADDED / MODIFIED / REMOVED Requirements`),每个需求带
  `#### Scenario:`。

### 6.2 两端同步实现
契约定稿后,**BR 端和设备端一起改**,保证对接:
- BR:`common/mqtt_ot_bridge/src/`(如 `registry_collect` 读 TXT)
- 设备:`ot_iot_device/main/`(如 `srp_register` 填 TXT)
- 每端各自 `idf.py build` + monitor 验证。

### 6.3 同步更新两端 README 的契约表
`common/mqtt_ot_bridge/README.md` 和 `ot_iot_device/README.md` 的 topic/payload
表必须一致——**没有中心真相,文档就是契约**。

### 6.4 归档 change
实现并硬件验证通过后:
```
openspec archive <change-name>      # 或用 /opsx:archive
```
这会把 delta spec 同步进 `openspec/specs/` 主 spec,并把 change 移入
`openspec/changes/archive/YYYY-MM-DD-<name>/`。

---

## 7. 验证清单(每次迭代收尾前)

本机无自动化测试,靠这份人工清单兜底:

- [ ] 涉及的每个工程 `idf.py build` 通过。
- [ ] 单机 monitor:启动日志链正常
      (BR: `coap start err=0` → `MQTT connected` → `subscribed` → `published registry`;
       设备: `SRP registration queued` → `CoAP started` → `subscribe multicast err=0`)。
- [ ] BR 的 `dev/registry` 能发现设备(或 BR CLI `ot srp server host/service` 查看)。
- [ ] 命令闭环:发命令 → 设备动作 → `dev/response` 回执 payload 正确。
- [ ] 组播(如涉及):多设备各自抖动后回执,`dev/response` 收到多条。
- [ ] 契约变更时:两端 README 契约表已同步、OpenSpec 已更新并归档。
- [ ] 相关 README/文档已更新。

---

## 8. 反复踩过的陷阱(改代码前必读)

来自真实联调,违反任一条都会崩或行为错乱:

1. **任务栈很小,别在回调/定时器里放大局部数组。** FreeRTOS 定时器任务栈约 2KB,
   放 4KB 数组直接 Stack protection fault。用 `calloc` 堆分配。
2. **别在 CoAP 回调里 `vTaskDelay`。** 回调跑在 OpenThread 任务上下文,阻塞它=阻塞
   整个协议栈。要延迟用 FreeRTOS 单次定时器承载。
3. **OT API 必须持锁。** CoAP/SRP 回调里已持锁(勿再 acquire);自建任务/定时器里
   要自己 `esp_openthread_lock_acquire/release`。
4. **回执用 CoAP NON,不靠 CON 的 ACK。** BR 下行是 NON;回执走"设备单播到 `/ack`"。
   若误用 CON 而对端不回 ACK,会重传→重复执行+重复上报。
5. **设备身份是 EUI64。** SRP host/instance 名 = 16 位小写十六进制 EUI64,BR 靠它
   路由和匹配。改命名规则=改契约(走流程 B)。
6. **Thread router 数 ≠ SRP 注册数。** `dev/registry` 只含跑了 SRP 注册的设备
   (即 `ot_iot_device`),这是设计使然,不是 bug。
7. **ESP32-H2-DevKitM-1 板载灯是 WS2812 可寻址 RGB(GPIO8),不是普通 GPIO 灯。**
   `gpio_set_level()` 驱不动它——灯完全不亮且不报错。必须用 `led_strip` 组件
   (RMT 时序):`led_strip_new_rmt_device` + `set_pixel/refresh`(亮)/`clear`(灭)。
   依赖声明 `espressif/led_strip`。参考 `ot_iot_device/main/device_switch.c`。

设备端更细的扩展陷阱见 `ot_iot_device/docs/DEVELOPMENT.md` 第 6 节。

---

## 9. 分支与提交约定

- 在功能分支上开发(如 `feat/<主题>`),不直接在 `main` 上改。
- 提交信息用 `feat/fix/docs/chore(scope): ...`,scope 用 `mqtt_ot_bridge` 或
  `ot_iot_device`;正文说明改了什么 + 如何验证。
- 完成后合并回 `main`(fast-forward 优先),删除功能分支。
- push 到远程前先确认——本仓库默认不自动 push。

---

## 10. 快速上手一次新迭代(TL;DR)

```
1. 判断改动范围 → 流程 A(单端) 还是 B(契约)         [第 4 节]
2. (流程 B) openspec change new + 写 proposal/design/specs/tasks
3. 建功能分支,改代码                                  [DEVELOPMENT.md]
4. PowerShell 里 idf.py build + flash monitor 验证    [第 3、7 节]
5. 端到端联调:命令→动作→dev/response                 [第 7 节]
6. 更新 README(契约变更同步两端)
7. (流程 B) openspec archive
8. 提交 → 合并 main → 删分支                           [第 9 节]
```
