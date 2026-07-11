## Context

设备端固件 `ot_iot_device`（由 `ot_cli` 派生）当前把 SRP 注册、CoAP 传输、命令解析、GPIO 驱动全平铺在 `main/`，命令协议是简化的 `{reqid,cmd:"on/off/query/blink"}`。BR 侧 `common/mqtt_ot_bridge` 已是"能力封装成 `common/` 组件"的范本（无状态双向管道，payload 透传）。本次目标是把设备端能力同样组件化，落地 `物联网IoT指令响应协议规范` 的核心部分：统一信封 + 可插拔外设指令面。周期上报（`sensor_report`/`report_freq`）本次不做，留待加入传感器能力时一并设计。

约束：
- BR 核心为 ESP-IDF 内预编译库；本仓库只在其上加组件/示例。
- 无主机端测试套件，验证靠编译 + 设备端 monitor。IDF v6.0.2。
- `idf.py` 须在 ESP-IDF PowerShell/CMD 跑，不能在 Git Bash。
- IDF v6 里 esp-mqtt / cJSON 是托管组件（`espressif/mqtt`、`espressif/cjson`）。

## Goals / Non-Goals

**Goals:**
- 设备端能力拆为可复用 `common/` 组件：一个协议+传输内核 + 一 event 一能力组件。
- 显式 `xxx_init()` 注册，项目按需 `REQUIRES` 能力组件即达成编译期功能可配置。
- 内核负责全部协议信封（`reqid`/`eui64`/`event_resp`/`code`/`msg`）与上报，能力组件只写业务。
- 内核用独立 worker 任务执行 handler，与 OpenThread 任务解耦，允许 handler 阻塞。
- 本次交付内核骨架 + 3 个样板能力（`switch`/`pwm_set`/`adc_read`），验证注册机制。

**Non-Goals:**
- 不在本次实现全部 12+ event（`switch_toggle`/`batch`/`gpio_read`/`dac_set`/`servo_set`/`ir_send` 留待按模板复制）。
- **周期上报框架、`report_freq_set/get`、`sensor_report` 主动上报本次推迟**：无样板生产者、无法端到端验证，留待加入传感器能力时连同 NVS 持久化一并设计。
- 不引入链接期自动注册（构造器段魔法）——保持显式调用。
- 不改变 BR 无状态转发的整体架构（`registry_list` 走专用 topic，是唯一例外）。
- 不做运行期动态引脚重映射的持久化（命令自带 `gpio`/`channel`）。

## Decisions

### D1: 分层——内核 + 可插拔能力
内核 `iot_device_core` 维护 `event 名 → handler` 分发表，不认识任何具体外设。能力组件在自己的 `init` 里调 `iot_device_register_handler(event, fn)` 挂载。
- **为何**：内核零改动即可加外设；项目按需引用组件 → 二进制只含所需能力。
- **备选**：单体固件（现状）——无法裁剪、无法复用，被否。

### D2: 能力粒度——一 event 一组件
`iot_cap_switch` / `iot_cap_pwm_set` / `iot_cap_adc_read`，每个组件对应协议里的一个 `event`。
- **为何**：粒度与协议 event 一一对应，项目引用即所见即所得。
- **备选**：一组件=一类外设（GPIO 含 switch/toggle/batch/read）——粒度粗、裁剪不到单 event，按用户选择否决。
- **共享驱动**（方案 C）：直接用 ESP-IDF driver 层（`gpio_set_level`/`gpio_get_level`/`ledc_*`/`adc_*`）。GPIO 为无状态硬件，读电平直接问硬件、不缓存；仅 LEDC 通道分配、ADC 校准句柄需组件内 static 状态。避免多组件重复缓存导致状态不一致。

### D3: 注册方式——显式调用
项目 `app_main` 里 `iot_device_core_start()` 后逐个 `iot_cap_xxx_init()`。
- **为何**：符合 ESP-IDF 惯例，可读、可调试、无链接魔法。
- **备选**：`__attribute__((constructor))` / IDF 构造器段自动注册——项目零代码但难调试，否。

### D4: handler 统一签名
```c
// 返回状态码：0 成功 / -1 参数错 / -2 忙 / -3 不支持 / -4 硬件异常
typedef int (*iot_event_handler_t)(const cJSON *data, cJSON *resp_data);
```
能力组件只读 `data`、干活、填 `resp_data`、返状态码；`reqid`/`eui64`/`event_resp`/`msg`/信封/上报全归内核。
- **为何**：职责清晰，能力组件不碰协议与传输。
- 内核据返回码填 `code` 与 `msg`（`success`/`fail`），拼 `event + "_resp"`，封根级 `eui64`，透传 `reqid`。

### D5: 上报器归内核
内核负责应答上报：worker 执行完 handler 后，内核封装响应信封并单播 NON POST 到 BR 的 `ack` 资源。能力组件不碰上报。
- **为何**：上报与信封同属协议层，集中在内核；能力只填 `resp_data`。
- 组播指令的上报加 0~500ms 随机抖动（FreeRTOS 单次定时器承载），避免多设备响应风暴。
- 周期性主动上报（`sensor_report`/`report_freq`）本次不做，见 Non-Goals。

### D9: handler 执行上下文——内核 worker 任务
内核创建独立 worker 任务。CoAP 回调（OT 任务上下文）只解析信封并把 `{reqid,event,data,是否组播,peer}` 投递到队列即返回；worker 取出后查表、调 handler、封装、上报。handler 跑在 worker 上下文，**允许阻塞**，访问 OT API 时自行持锁。worker 栈深经 Kconfig 配置。
- **为何**：已吃过"阻塞 OT 任务/定时器栈溢出"的亏（见 mqtt_ot_bridge 历史 bug）。ADC 采样、LEDC 配置等可能毫秒级阻塞，放 OT 回调里会拖住协议栈。worker 把所有能力与 OT 栈解耦，栈可控、handler 可安心阻塞。
- **备选 A**（同步、约定 handler 必须快）：最简单但 adc/未来 ir/servo 有阻塞风险，否。
- **备选 C**（快慢两种注册）：灵活但 API 复杂、易误用，否。
- 队列满时丢弃并记日志，不阻塞 CoAP 回调。

### D6: BR 应答 registry_list（专用 topic）
只有 BR 能看到 SRP 表。BR 订阅专用 topic `cmd/registry`，收到 `registry_list` 即查 SRP 表回 `registry_list_resp` 到 `cmd/resp`。现有 retained `dev/registry` 主题保留。
- **为何**：规范把清单建模为请求/响应指令，且清单查询本就是 BR 独有职责。用**专用 topic** 隔离，使 BR 只需解析 `cmd/registry` 一路，控制类下行（`cmd/unicast/+`、`cmd/multicast`）仍是纯字节透传，不必 JSON 解析每条下行。
- **代价**：BR 多解析一个 topic，透传原则加一条边界清晰的例外。
- **备选**：让 registry_list 混在 `cmd/multicast` 里 → BR 必须解析每条组播下行，侵蚀纯管道，否。仅保留 retained 主题（偏离规范）亦否。

### D7: 主题变更
- 上行响应 `dev/response` → `cmd/resp`（对齐规范 `otbr/cmd/resp`）。
- 新增专用下行 topic `cmd/registry`（仅承载 `registry_list`，BR 自应答）。
- 单播/组播下行主题不变（已与规范一致）。
- `dev/up` 主动上报主题本次不引入（无生产者），随周期上报一并推迟。

### D8: ot_iot_device 直接改造（BREAKING）
弃用 `{reqid,cmd}`，改用 `{reqid,event,data}`。旧 `on/off/query/blink` 映射到 `switch` event（`action:"on"/"off"`；blink 用 `switch` + `delay`/`hold` 表达或保留为独立行为映射）。

## Risks / Trade-offs

- [BR 解析下行] → 仅 `cmd/registry` 专用 topic 解析，控制类下行仍纯透传；边界在 topic 层隔离，代码里独立分支。
- [BREAKING 协议不兼容] → 设备端与 BR 同一 change 内同步改；云端需同步联调，proposal 已标注 Impact。
- [细粒度组件数量多] → 本次只做 3 样板 + 内核；其余按模板复制，模板在 design 固化，降低复制成本。
- [定时器任务栈小] → 沿用既有教训：上报缓冲用堆分配，不在定时器/回调任务放大局部数组（参见 mqtt_ot_bridge 历史 bug）。
- [组播响应风暴] → 沿用设备端 0~500ms 随机抖动 + 单次定时器承载，不在 CoAP 回调 vTaskDelay。
- [IDF v6 组件命名] → CMake REQUIRES 用 `espressif__cjson`；依赖声明 `espressif/cjson`；头 `cJSON.h`。

## Migration Plan

1. 建 `iot_device_core`（内核骨架 + 分发表 + worker 任务 + 信封 + 应答上报）。
2. 建 3 个样板能力组件，各自 `init` 注册。
3. 改造 `ot_iot_device/main`：显式挂载内核 + 3 能力，删旧 `{cmd}` 解析。
4. 改 `mqtt_ot_bridge`：响应主题改名 `cmd/resp`、新增 `cmd/registry` 订阅与 `registry_list` 应答分支 + Kconfig。
5. 编译验证（BR + H2 设备），设备端 monitor 联调单播/组播/查询/registry 全链路。
- 回滚：本 change 为独立分支，未合并前可整体丢弃；BR 主题变更与设备协议绑定，须同版本部署。

## Open Questions

- `blink` 在新协议下如何表达：映射为 `switch` + `hold`（点亮后自动关）？还是保留独立 event？倾向前者，落 spec 时定。
