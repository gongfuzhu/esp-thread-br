# MQTT over TLS (emqxsl) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `mqtt_ot_bridge` 组件能以 `mqtts://` + 嵌入的 CA 证书连接 EMQX Cloud Serverless broker，并把 `basic_thread_border_router` 的连接参数指向该 broker。

**Architecture:** 在组件内嵌入用户提供的 CA PEM（DigiCert Global Root G2），通过 `EMBED_TXTFILES` 打入固件；`mqtt_ot_bridge_start()` 在 `esp_mqtt_client_config_t` 中挂上 `broker.verification.certificate`。TLS 是否启用由 broker URI 的 scheme（`mqtts://` vs `mqtt://`）驱动，因此明文连接向后兼容自然成立，无需分支逻辑。连接参数（URI/用户名/密码）在 `sdkconfig.defaults` 覆盖。

**Tech Stack:** ESP-IDF v6.0.2、`espressif__mqtt`（esp-mqtt 托管组件）、mbedTLS（IDF 内置）、OpenThread BR。

## Global Constraints

- ESP-IDF 环境：`source /d/esp/v6.0.2/esp-idf/export.sh`（Git Bash）后方可编译。
- **不手改 `sdkconfig`**（自动生成）；配置覆盖只写 `sdkconfig.defaults*`。
- **无主机端测试套件**；每个任务的验证门是 `idf.py build` 编译通过 + 符号/配置检查，最终由设备端 `monitor` 确认握手。
- esp-mqtt 字段（已核对 `managed_components/espressif__mqtt/include/mqtt_client.h`）：`cfg.broker.verification.certificate`（`const char *`）、`cfg.broker.verification.certificate_len`（`size_t`）。头文件注明 PEM 必须以 NUL 结尾——`EMBED_TXTFILES` 会自动追加 NUL 终止符。
- CA 证书：DigiCert Global Root G2，有效期至 2038-01-15。
- 连接目标：`mqtts://ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883`，user `gfz`，pass `QWERasdf`。
- 所有命令在 `basic_thread_border_router/` 目录下执行（除非另有说明）。

---

### Task 1: 嵌入 CA 证书文件

**Files:**
- Create: `common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`
- Modify: `common/mqtt_ot_bridge/CMakeLists.txt`

**Interfaces:**
- Consumes: 无。
- Produces: 嵌入固件的链接符号 `_binary_mqtt_ca_cert_pem_start` 与 `_binary_mqtt_ca_cert_pem_end`（供 Task 2 引用）。符号名由文件名 `mqtt_ca_cert.pem` 派生，与 `main` 中 OTA 用的 `ca_cert.pem` 不冲突。

- [ ] **Step 1: 创建证书文件**

创建 `common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`，内容为（完整 PEM，含首尾行与结尾换行）：

```
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
```

- [ ] **Step 2: 修改组件 CMakeLists.txt 嵌入证书**

`common/mqtt_ot_bridge/CMakeLists.txt` 当前内容：

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES openthread
                       PRIV_REQUIRES esp_netif freertos esp_event espressif__mqtt espressif__cjson)
```

改为（新增 `EMBED_TXTFILES`）：

```cmake
idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES openthread
                       PRIV_REQUIRES esp_netif freertos esp_event espressif__mqtt espressif__cjson
                       EMBED_TXTFILES "certs/mqtt_ca_cert.pem")
```

- [ ] **Step 3: 验证证书文件格式**

Run: `openssl x509 -in common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem -noout -subject -enddate`
Expected: 输出 `subject=... CN = DigiCert Global Root G2` 且 `notAfter=Jan 15 12:00:00 2038 GMT`（确认 PEM 有效、未被换行破坏）。

- [ ] **Step 4: Commit**

```bash
git add common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem common/mqtt_ot_bridge/CMakeLists.txt
git commit -m "feat(mqtt_ot_bridge): embed DigiCert CA cert for TLS"
```

---

### Task 2: 组件启用 TLS 证书验证

**Files:**
- Modify: `common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`（顶部 extern 声明区，约 line 26 附近；`mqtt_ot_bridge_start()` 内 cfg 初始化，约 line 355）

**Interfaces:**
- Consumes: Task 1 产出的链接符号 `_binary_mqtt_ca_cert_pem_start` / `_binary_mqtt_ca_cert_pem_end`。
- Produces: 无新增对外接口；`mqtt_ot_bridge_start(void)` 签名不变（仍返回 `esp_err_t`）。

- [ ] **Step 1: 添加 extern 符号声明**

在 `mqtt_ot_bridge.c` 的 `#define TAG "mqtt_ot_bridge"`（line 21）之后、`static esp_mqtt_client_handle_t s_client` 之前，加入：

```c
// TLS CA 证书（由组件 CMakeLists.txt 的 EMBED_TXTFILES 嵌入）
extern const uint8_t mqtt_ca_cert_pem_start[] asm("_binary_mqtt_ca_cert_pem_start");
extern const uint8_t mqtt_ca_cert_pem_end[]   asm("_binary_mqtt_ca_cert_pem_end");
```

- [ ] **Step 2: 在 client config 挂上 CA 证书**

`mqtt_ot_bridge_start()` 中现有 cfg 初始化（line 355-359）：

```c
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_OT_BRIDGE_BROKER_URI,
        .credentials.username = CONFIG_MQTT_OT_BRIDGE_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_OT_BRIDGE_PASSWORD,
    };
```

改为（新增 `broker.verification.certificate` 与长度）：

```c
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_OT_BRIDGE_BROKER_URI,
        // mqtts:// 时用嵌入的 CA 校验服务器证书；mqtt:// 时 esp-mqtt 忽略此字段（明文向后兼容）
        .broker.verification.certificate = (const char *)mqtt_ca_cert_pem_start,
        .broker.verification.certificate_len = mqtt_ca_cert_pem_end - mqtt_ca_cert_pem_start,
        .credentials.username = CONFIG_MQTT_OT_BRIDGE_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_OT_BRIDGE_PASSWORD,
    };
```

> 说明：`EMBED_TXTFILES` 会追加 NUL 终止符，`_end - _start` 因此包含结尾 NUL。esp-mqtt 头文件说明 PEM 需 NUL 结尾；传入的 `certificate_len` 覆盖到 NUL 是安全的，esp-tls 按 PEM 解析。

- [ ] **Step 3: 编译组件（快速门，暂不烧录）**

Run:
```bash
source /d/esp/v6.0.2/esp-idf/export.sh
idf.py build
```
Expected: 编译成功，无 `undefined reference to _binary_mqtt_ca_cert_pem_start`、无 `broker` 结构体字段错误。

> 若报链接符号未定义：确认 Task 1 Step 2 的 `EMBED_TXTFILES` 已保存且路径相对组件根为 `certs/mqtt_ca_cert.pem`。

- [ ] **Step 4: Commit**

```bash
git add common/mqtt_ot_bridge/src/mqtt_ot_bridge.c
git commit -m "feat(mqtt_ot_bridge): verify broker cert with embedded CA on mqtts"
```

---

### Task 3: 更新 broker 连接参数

**Files:**
- Modify: `basic_thread_border_router/sdkconfig.defaults`（末尾 `# MQTT OT Bridge` 段，line 140-141）

**Interfaces:**
- Consumes: Task 2 已能处理 `mqtts://`。
- Produces: 运行时 `CONFIG_MQTT_OT_BRIDGE_BROKER_URI/_USERNAME/_PASSWORD` 三个值。

- [ ] **Step 1: 追加连接参数覆盖**

`sdkconfig.defaults` 末尾现有：

```
# MQTT OT Bridge
CONFIG_MQTT_OT_BRIDGE_ENABLE=y
```

改为：

```
# MQTT OT Bridge
CONFIG_MQTT_OT_BRIDGE_ENABLE=y
CONFIG_MQTT_OT_BRIDGE_BROKER_URI="mqtts://ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883"
CONFIG_MQTT_OT_BRIDGE_USERNAME="gfz"
CONFIG_MQTT_OT_BRIDGE_PASSWORD="QWERasdf"
```

- [ ] **Step 2: 重新生成配置并确认取值**

Run:
```bash
idf.py reconfigure
grep -E "MQTT_OT_BRIDGE_(BROKER_URI|USERNAME|PASSWORD)" build/config/sdkconfig.h
```
Expected: 输出三行，`CONFIG_MQTT_OT_BRIDGE_BROKER_URI "mqtts://ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883"`、`... "gfz"`、`... "QWERasdf"`（确认覆盖生效，未被 Kconfig 默认值遮盖）。

- [ ] **Step 3: Commit**

```bash
git add basic_thread_border_router/sdkconfig.defaults
git commit -m "config(basic_br): point mqtt bridge to emqxsl over TLS"
```

---

### Task 4: 更新文档与 Kconfig 帮助文本

**Files:**
- Modify: `common/mqtt_ot_bridge/Kconfig.projbuild`（line 12-16 的 `MQTT_OT_BRIDGE_BROKER_URI` 帮助文本）
- Modify: `common/mqtt_ot_bridge/README.md`（line 10 的 BROKER_URI 描述、以及 line 46 数据流图的「MQTT(登陆)」注释）

**Interfaces:**
- Consumes: 无。
- Produces: 无代码接口，仅文档。

- [ ] **Step 1: 更新 Kconfig 帮助文本**

`Kconfig.projbuild` 中 `MQTT_OT_BRIDGE_BROKER_URI` 段（line 12-16）：

```
        config MQTT_OT_BRIDGE_BROKER_URI
            string "MQTT broker URI"
            default "mqtt://192.168.1.100:1883"
            help
                局域网 broker 地址，形如 mqtt://host:port(不使用 TLS)。
```

改为：

```
        config MQTT_OT_BRIDGE_BROKER_URI
            string "MQTT broker URI"
            default "mqtt://192.168.1.100:1883"
            help
                broker 地址。mqtt://host:port 为明文；mqtts://host:port 启用 TLS,
                此时用组件内嵌入的 CA 证书(certs/mqtt_ca_cert.pem)校验服务器证书。
```

- [ ] **Step 2: 更新 README broker 描述**

`README.md` line 10：

```
- `MQTT_OT_BRIDGE_BROKER_URI`：`mqtt://host:port`(局域网，无 TLS)
```

改为：

```
- `MQTT_OT_BRIDGE_BROKER_URI`：`mqtt://host:port`(明文) 或 `mqtts://host:port`(TLS，用组件内嵌 CA 校验)
```

- [ ] **Step 3: 更新 README 数据流图注释**

`README.md` line 46：

```
服务端 --MQTT(登陆)--> BR --CoAP--> H2 设备
```

改为：

```
服务端 --MQTT/MQTTS(登陆)--> BR --CoAP--> H2 设备
```

- [ ] **Step 4: Commit**

```bash
git add common/mqtt_ot_bridge/Kconfig.projbuild common/mqtt_ot_bridge/README.md
git commit -m "docs(mqtt_ot_bridge): document mqtts/TLS support"
```

---

### Task 5: 整机验证（编译 + 设备端握手）

**Files:**
- 无修改；验证任务。

**Interfaces:**
- Consumes: Task 1-4 的全部产出。
- Produces: 验证结论。

- [ ] **Step 1: 全量编译**

Run:
```bash
source /d/esp/v6.0.2/esp-idf/export.sh
idf.py build
```
Expected: `Project build complete`，生成 `build/*.bin`。

- [ ] **Step 2: 烧录并监视**

Run（`<PORT>` 替换为实际串口，如 `COM5`）：
```bash
idf.py -p <PORT> flash monitor
```

- [ ] **Step 3: 确认 TLS 连接成功**

在 monitor 输出中确认（设备需已联网、Thread/BR 已起）：
- 有 `mqtt_ot_bridge` 或 esp-mqtt 的 `MQTT_EVENT_CONNECTED` 日志（无 `MQTT_EVENT_ERROR` 的 TLS 握手失败）。
- 无 `esp-tls: Failed to verify peer certificate` / `mbedtls ... X509 - Certificate verification failed` 报错。

Expected: 连接建立并订阅下行 topic。

> 排查提示：若出现证书校验失败，最可能是 broker 由**中间 CA** 签发而握手未收到完整链——确认 emqxsl 侧下发中间证书；或临时用 `esp_crt_bundle_attach` 交叉验证 CA 是否匹配。此为 design.md 记录的首要风险项。

- [ ] **Step 4: 归档 OpenSpec change（可选，验证通过后）**

Run:
```bash
openspec archive add-mqtt-tls
```
Expected: change 移入 archive，`mqtt-bridge` spec 的 MODIFIED requirement 合并入主 spec。
