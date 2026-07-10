# 修复 SoftAP 配网二次 esp_wifi_init abort — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让开启 SoftAP 配网（`CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y`）且空 NVS 的设备，经网页提交 Wi-Fi 凭据后能干净初始化 STA 并联网，不再因二次 `esp_wifi_init` 触发 `ESP_ERR_INVALID_STATE` abort。

**Architecture:** 根因是两个「Wi-Fi 已初始化」状态语义错位——SoftAP 侧的 `esp_wifi_init` 推进了 IDF driver 状态，但 `esp_ot_cli_extension` 的静态标志 `s_wifi_initialized` 仍为 false。修法：新增只读 getter 暴露该标志，`wifi_config_stop_softap()` 据此分支——STA 未初始化则 `esp_wifi_deinit()` 使 driver 归零（让后续 `example_wifi_start` 从头干净初始化），STA 已初始化则保持原 `set_mode(STA)+start`。不改 IDF 代码。

**Tech Stack:** ESP-IDF v6.0.2、esp_wifi、esp_netif、两个本仓库组件 `esp_ot_cli_extension` 与 `esp_ot_br_server`。

## Global Constraints

- ESP-IDF 环境由用户在 EIM/PowerShell 下激活（bash 侧 `export.sh` 不可用）；实现者**不运行** `idf.py`，编译/烧录由用户完成。
- 不手改 `sdkconfig`（自动生成）；配置覆盖只写 `sdkconfig.defaults*`。
- **不修改 IDF 代码**（`example_wifi_start` / `wifi_connect.c` 保持原样）。
- 无主机测试套件；验证靠 `idf.py build` + 设备端 monitor。
- `esp_ot_wifi_cmd.c` 仅在 `CONFIG_OPENTHREAD_CLI_WIFI=y` 时编译；新增 getter 必须处于同一编译单元，自然受此条件约束。
- 静态标志 `s_wifi_initialized`（`esp_ot_wifi_cmd.c:77`）语义 = STA 侧是否已完成 `example_wifi_start` 初始化；写入仍集中在 `esp_ot_wifi_connect` 一处，本次只新增**只读** getter，不新增 setter。
- `esp_wifi_deinit(void)` 返回 `esp_err_t`；`esp_wifi_stop(void)` 返回 `esp_err_t`。

---

### Task 1: 暴露 STA 初始化状态（只读 getter）

**Files:**
- Modify: `components/esp_ot_cli_extension/include/esp_ot_wifi_cmd.h`（在 `esp_ot_wifi_state_get` 声明后、`#ifdef __cplusplus` 收尾前，约 127-129 行之间新增声明）
- Modify: `components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c`（在文件中新增函数定义；`s_wifi_initialized` 定义在 77 行）

**Interfaces:**
- Consumes: 私有静态标志 `s_wifi_initialized`（同一文件内可见）。
- Produces: `bool esp_ot_wifi_is_sta_initialized(void)` — 返回 STA 侧是否已初始化（Task 2 跨组件调用）。

- [ ] **Step 1: 在头文件声明 getter**

在 `components/esp_ot_cli_extension/include/esp_ot_wifi_cmd.h` 的 `esp_ot_wifi_state_get` 声明（127 行）之后、`#ifdef __cplusplus`（129 行）之前，插入：

```c

/**
 * @brief This function reports whether the Wi-Fi STA side has been initialized
 *        (i.e. example_wifi_start has run and the STA netif exists).
 *
 * @return
 *      true if the STA side is initialized, false otherwise
 */
bool esp_ot_wifi_is_sta_initialized(void);
```

- [ ] **Step 2: 在源文件定义 getter**

在 `components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c` 中，紧接 `esp_ot_wifi_state_get` 函数定义之后（即 `esp_ot_wifi_state_get` 的 `return s_wifi_state; }` 之后，源码约 443 行附近）新增：

```c
bool esp_ot_wifi_is_sta_initialized(void)
{
    return s_wifi_initialized;
}
```

> 说明：`bool` 类型已通过 `<stdbool.h>`（经 `esp_err.h`/`sdkconfig.h` 链间接可用，文件中已大量使用 `bool`）。无需新增 include。

- [ ] **Step 3: 静态审查（无编译环境）**

实现者无法运行 `idf.py`。改为静态确认：
- 头文件声明与源文件定义的签名逐字一致：`bool esp_ot_wifi_is_sta_initialized(void)`。
- 定义位于 `esp_ot_wifi_cmd.c` 内（受 `CONFIG_OPENTHREAD_CLI_WIFI` 编译条件覆盖），且函数体只读 `s_wifi_initialized`、不写入。

Run: `grep -n "esp_ot_wifi_is_sta_initialized" components/esp_ot_cli_extension/include/esp_ot_wifi_cmd.h components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c`
Expected: 头文件 1 处声明、源文件 1 处定义，签名一致。

- [ ] **Step 4: Commit**

```bash
git add components/esp_ot_cli_extension/include/esp_ot_wifi_cmd.h components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c
git commit -m "feat(cli_ext): expose esp_ot_wifi_is_sta_initialized getter"
```

---

### Task 2: 声明组件依赖并对齐 stop_softap 的 Wi-Fi driver 状态

**Files:**
- Modify: `components/esp_ot_br_server/CMakeLists.txt`（`requires` 列表，第 1 行）
- Modify: `components/esp_ot_br_server/src/esp_br_wifi_config.c`（include 区约 30-36 行；`wifi_config_stop_softap` 约 267-292 行，其中 STA 恢复逻辑在 278-284 行）

**Interfaces:**
- Consumes: `esp_ot_wifi_is_sta_initialized()`（Task 1）；`esp_wifi_deinit()`、`esp_wifi_stop()`、`esp_wifi_set_mode()`、`esp_wifi_start()`（esp_wifi）。
- Produces: 无对外接口；`wifi_config_stop_softap()` 仍是 `static void`，签名不变。

- [ ] **Step 1: 给 esp_ot_br_server 加 esp_ot_cli_extension 依赖**

`components/esp_ot_br_server/CMakeLists.txt` 第 1 行当前为：

```cmake
set(requires mdns fatfs spiffs esp_eth nvs_flash freertos openthread esp_http_server protocol_examples_common esp_wifi esp_app_format esp_timer)
```

在末尾追加 `esp_ot_cli_extension`：

```cmake
set(requires mdns fatfs spiffs esp_eth nvs_flash freertos openthread esp_http_server protocol_examples_common esp_wifi esp_app_format esp_timer esp_ot_cli_extension)
```

> 前置确认（已核实）：`esp_ot_cli_extension` 不依赖 `esp_ot_br_server`，故此依赖不成环。

- [ ] **Step 2: 在 esp_br_wifi_config.c 包含 getter 头文件**

`components/esp_ot_br_server/src/esp_br_wifi_config.c` 的 include 区（30-36 行，`#include "esp_netif.h"` 到 `#include "lwip/sockets.h"` 之间）新增一行，与既有风格一致：

```c
#include "esp_ot_wifi_cmd.h"
```

放在 `#include "esp_netif.h"`（30 行）之后即可。

- [ ] **Step 3: 改 wifi_config_stop_softap 按 STA 状态分支**

`components/esp_ot_br_server/src/esp_br_wifi_config.c` 的 `wifi_config_stop_softap()` 中，现有 278-284 行为：

```c
    esp_wifi_stop();

    /* Always restore STA mode rather than calling esp_wifi_deinit(). If we deinit here, the
     * managed component's s_wifi_initialized flag remains true, causing the next
     * esp_ot_wifi_connect() to skip example_wifi_start() and fail with ESP_ERR_WIFI_NOT_INIT. */
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
```

替换为：

```c
    esp_wifi_stop();

    /* Align the IDF Wi-Fi driver state with the STA-side init flag (s_wifi_initialized).
     *
     * - STA NOT initialized (auto-start provisioning): the SoftAP path called esp_wifi_init()
     *   but the STA side never ran example_wifi_start(), so no STA netif exists. Deinit the
     *   driver so the subsequent esp_ot_wifi_connect() -> example_wifi_start() can initialize
     *   cleanly (create the STA netif) instead of aborting on ESP_ERR_INVALID_STATE.
     * - STA already initialized (CLI `wifi connect` path): keep the driver initialized and
     *   restore STA mode; deinit here would make the next connect fail with ESP_ERR_WIFI_NOT_INIT. */
    if (!esp_ot_wifi_is_sta_initialized()) {
        esp_wifi_deinit();
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }
```

> 保留 `esp_wifi_stop()` 在分支之前（两条路径都需要先停）。AP netif 的 destroy 在其后（286-289 行）保持不动，两分支都会执行。

- [ ] **Step 4: 静态审查（无编译环境）**

实现者无法运行 `idf.py`。改为静态确认：
- `esp_ot_br_server/CMakeLists.txt` 的 `requires` 含 `esp_ot_cli_extension`。
- `esp_br_wifi_config.c` 已 `#include "esp_ot_wifi_cmd.h"`。
- `wifi_config_stop_softap` 中 `esp_wifi_stop()` 仍在分支前；`else` 分支的 `set_mode(STA)+start` 与原逻辑逐字一致；`if` 分支为单独 `esp_wifi_deinit()`。
- AP netif destroy（`if (s_ap_netif) { esp_netif_destroy(...); s_ap_netif = NULL; }`）仍在函数尾部、两分支之后。

Run: `grep -n "esp_ot_wifi_is_sta_initialized\|esp_wifi_deinit\|esp_wifi_set_mode\|esp_ot_cli_extension" components/esp_ot_br_server/CMakeLists.txt components/esp_ot_br_server/src/esp_br_wifi_config.c`
Expected: CMakeLists 含依赖；`esp_br_wifi_config.c` 中 getter 调用 1 处、`esp_wifi_deinit` 1 处、`esp_wifi_set_mode` 1 处（在 else 分支）。

- [ ] **Step 5: Commit**

```bash
git add components/esp_ot_br_server/CMakeLists.txt components/esp_ot_br_server/src/esp_br_wifi_config.c
git commit -m "fix(br_server): deinit Wi-Fi on SoftAP exit when STA not yet initialized

Aligns the IDF Wi-Fi driver state with the STA-side s_wifi_initialized
flag so the auto-start provisioning path can re-run example_wifi_start
cleanly instead of aborting on a second esp_wifi_init (ESP_ERR_INVALID_STATE).
CLI wifi-connect path keeps its existing set_mode(STA)+start behavior."
```

---

### Task 3: 设备验证（用户执行）

**Files:**
- 无源码修改；验证任务。临时改动 `examples/basic_thread_border_router/sdkconfig.defaults`（验证后还原）。

**Interfaces:**
- Consumes: Task 1、2 的全部产出。
- Produces: 验证结论。

- [ ] **Step 1: 临时启用 SoftAP 配网**

在 `examples/basic_thread_border_router/sdkconfig.defaults` 中，将 SoftAP 一行由禁用改回启用以验证该路径。当前（约 105-110 行）为多行注释 + `# CONFIG_OPENTHREAD_BR_SOFTAP_SETUP is not set`；临时改为：

```
CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y
```

> 这是临时验证配置，Step 5 会还原。因改了 `sdkconfig.defaults`，需删除生成的 `sdkconfig` 让其重建：`rm examples/basic_thread_border_router/sdkconfig`（该文件自动生成、不入库）。

- [ ] **Step 2: 编译烧录（用户在 EIM/PowerShell 环境）**

在 `basic_thread_border_router` 目录：

```
idf.py -p <PORT> build erase-flash flash monitor
```

`erase-flash` 清空 NVS，确保走「空 NVS + SoftAP」路径。

- [ ] **Step 3: 网页配网并确认不再 abort**

开机后设备启动 SoftAP `ESP-ThreadBR-XXXX`。用手机/电脑连该热点，浏览器访问 `http://192.168.4.1`，提交一个可用的 2.4GHz Wi-Fi SSID/密码。

Expected：
- 提交后 monitor **不出现** `wifi_init: Failed to init, WiFi is initialized by esp_wifi_init` 或 `ESP_ERR_INVALID_STATE at example_wifi_start` 的 panic。
- 出现 STA 连接与获取 IP 的日志（`example_connect`/`esp_netif` sta 拿到 IP）。

- [ ] **Step 4: 确认 CLI 路径未劣化（可选，若终端支持）**

若使用支持转义序列的终端（Windows Terminal / PuTTY），可先 `wifi connect -s <ssid> -p <psk>` 建立 STA 连接、再触发 SoftAP 流程，确认退出 SoftAP 后不出现 `ESP_ERR_WIFI_NOT_INIT`。若终端不支持 CLI 交互，跳过并在结论中注明「else 分支仅代码审查覆盖」。

- [ ] **Step 5: 还原临时配置**

把 `sdkconfig.defaults` 的 SoftAP 一行还原为禁用状态（连同其上的多行说明注释），恢复 `# CONFIG_OPENTHREAD_BR_SOFTAP_SETUP is not set`。删除生成的 `sdkconfig` 或重新 `idf.py reconfigure`，确认默认预设 Wi-Fi 路径仍正常。此步不产生入库改动（`sdkconfig.defaults` 回到修改前内容）。
