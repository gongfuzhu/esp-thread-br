# 物联网IoT指令响应协议规范（OpenThread\+ESP\-IDF 最终完整版）

## 一、协议概述

本协议适用于**OpenThread IPv6组网 \+ ESP\-IDF设备** 物联网场景，定义云端与终端设备的全双工通信标准。统一 MQTT 主题、报文结构、事件命名、状态码、设备溯源字段，支持单设备精准控制、多设备组播批量控制、设备注册列表查询，完全适配前后端联调、设备解析、日志溯源、业务配对。

## 二、MQTT 主题路由规范（固定不变）

全网三类主题，严格区分下行指令、上行响应，无路由冲突：

- **单播下行（云端→单个设备）**：`otbr/cmd/unicast/<eui64>`
用途：单点精准控制、单设备参数查询、单独配置下发
       
变量：\&lt;eui64\&gt; 为设备唯一硬件标识
      

- **组播下行（云端→所有设备）**：`otbr/cmd/multicast`
用途：全网设备批量控制、统一全局配置、广播指令下发
      

- **设备响应上行（设备→云端）**：`otbr/cmd/resp`
用途：所有指令执行回执、数据查询应答，所有 `xxx_resp` 响应统一上报此主题

- **设备主动上报（设备→云端）****`otbr/dev/up`**

用途：用于设备主动上报
      

## 三、全局统一报文格式（强制规范）

### 3\.1 下行指令格式（云端下发）

所有控制、查询、配置下行指令，结构统一，无字段缺失：

```Plain Text
{
  "reqid": "唯一请求批号",
  "event": "指令事件名",
  "data": {}
}
```

字段说明：

- reqid：全局唯一，用于上下行消息配对

- event：设备执行事件标识

- data：事件私有参数体，不同事件字段不同

### 3\.2 上行响应格式（设备上报，核心规范）

**所有响应报文根节点强制携带 eui64 字段**，用于云端精准溯源执行设备，格式全局统一：

```Plain Text
{
  "reqid": "与下行指令完全一致",
  "eui64": "设备唯一EUI64硬件标识",
  "event": "xxx_resp",
  "code": 0,
  "msg": "success / fail",
  "data": {}
}
```

### 3\.3 全局状态码定义

- **0**：执行成功

- **\-1**：参数错误

- **\-2**：设备忙/执行失败

- **\-3**：不支持该事件

- **\-4**：硬件异常

### 3\.4 协议强制规则

- 下行事件：原生事件名；上行响应事件名：原生事件名 \+ **\_resp** 后缀

- 应答类消息 reqid 必须与下行指令一致，实现消息配对

- 所有应答响应必须携带根级 eui64 设备标识字段

- 主动上报类消息无需 reqid，应答类消息严格遵循统一模板

---

## 四、设备注册列表事件

### 4\.1 查询组网注册设备 registry\_list

**下行指令**

```Plain Text
{
  "reqid": "batch-0001",
  "event": "registry_list",
  "data": {}
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0001",
  "eui64": "2222",
  "event": "registry_list_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "list": [
      {
        "eui64": "744dbdfffe664fc4",
        "ipv6": "fd32:ce52:9bc9:1:2d34:531e:d72a:c9c7",
        "service": "_iot._udp.default.service.arpa."
      },
      {
        "eui64": "744dbdfffe621c77",
        "ipv6": "fd32:ce52:9bc9:1:6745:c57:a3b7:b9ff",
        "service": "_iot._udp.default.service.arpa."
      },
      {
        "eui64": "744dbdfffe664e57",
        "ipv6": "fd32:ce52:9bc9:1:23ec:8ee9:42ff:1666",
        "service": "_iot._udp.default.service.arpa."
      }
    ]
  }
}
```

---

## 五、GPIO 控制类事件

### 5\.1 单路开关控制 switch

**下行指令**

```Plain Text
{
  "reqid": "batch-0002",
  "event": "switch",
  "data": {
    "gpio": 2,
    "action": "on",
    "delay": 0,
    "hold": 0
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0002",
  "eui64": "2222",
  "event": "switch_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 2,
    "status": "on"
  }
}
```

### 5\.2 GPIO电平翻转 switch\_toggle

**下行指令**

```Plain Text
{
  "reqid": "batch-0003",
  "event": "switch_toggle",
  "data": {
    "gpio": 2
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0003",
  "eui64": "2222",
  "event": "switch_toggle_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 2,
    "status": "toggle_ok"
  }
}
```

### 5\.3 多路批量开关 switch\_batch

**下行指令**

```Plain Text
{
  "reqid": "batch-0004",
  "event": "switch_batch",
  "data": {
    "list": [
      {"gpio": 2, "action": "on"},
      {"gpio": 4, "action": "off"}
    ]
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0004",
  "eui64": "2222",
  "event": "switch_batch_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "result": [
      {"gpio": 2, "status": "on"},
      {"gpio": 4, "status": "off"}
    ]
  }
}
```

### 5\.4 GPIO输入读取 gpio\_read

**下行指令**

```Plain Text
{
  "reqid": "batch-0005",
  "event": "gpio_read",
  "data": {
    "gpio": 3
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0005",
  "eui64": "2222",
  "event": "gpio_read_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 3,
    "level": 1
  }
}
```

---

## 六、PWM调光调速事件

### 6\.1 PWM参数配置 pwm\_set

**下行指令**

```Plain Text
{
  "reqid": "batch-0006",
  "event": "pwm_set",
  "data": {
    "gpio": 5,
    "freq": 1000,
    "duty": 60
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0006",
  "eui64": "2222",
  "event": "pwm_set_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 5,
    "freq": 1000,
    "duty": 60
  }
}
```

---

## 七、ADC/DAC模拟量事件

### 7\.1 ADC模拟量读取 adc\_read

**下行指令**

```Plain Text
{
  "reqid": "batch-0007",
  "event": "adc_read",
  "data": {
    "channel": 0
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0007",
  "eui64": "2222",
  "event": "adc_read_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "channel": 0,
    "raw_val": 2048,
    "voltage": 1.65
  }
}
```

### 7\.2 DAC模拟量输出 dac\_set

**下行指令**

```Plain Text
{
  "reqid": "batch-0008",
  "event": "dac_set",
  "data": {
    "channel": 0,
    "voltage": 2.5
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0008",
  "eui64": "2222",
  "event": "dac_set_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "channel": 0,
    "voltage": 2.5
  }
}
```

---

## 八、舵机控制事件

### 8\.1 舵机角度控制 servo\_set

**下行指令**

```Plain Text
{
  "reqid": "batch-0009",
  "event": "servo_set",
  "data": {
    "gpio": 13,
    "angle": 90
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0009",
  "eui64": "2222",
  "event": "servo_set_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 13,
    "angle": 90
  }
}
```

---

## 九、红外线控制事件

### 9\.1 红外码发送 ir\_send

**下行指令**

```Plain Text
{
  "reqid": "batch-0010",
  "event": "ir_send",
  "data": {
    "gpio": 17,
    "ir_code": "0x00FF12ED"
  }
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0010",
  "eui64": "2222",
  "event": "ir_send_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "gpio": 17,
    "send_status": "ok"
  }
}
```

---

## 十、主动上报频率控制事件

### 10\.1 设置设备主动上报频率 report\_freq\_set

**功能说明**：云端下发指令，配置设备各类主动上报事件的推送时间间隔，支持单独配置传感器上报、设备状态上报、告警上报频率，动态生效。

**下行指令**

```Plain Text
{
  "reqid": "batch-0011",
  "event": "report_freq_set",
  "data": {
    "sensor_report_ms": 5000,
    "status_report_ms": 10000,
    "alarm_report_ms": 1000
  }
}
```

**字段说明**

- sensor\_report\_ms：传感器数据上报间隔（单位：毫秒）

- status\_report\_ms：设备在线状态、电压、信号上报间隔（单位：毫秒）

- alarm\_report\_ms：设备告警事件上报间隔（单位：毫秒）

**上行响应**

```Plain Text
{
  "reqid": "batch-0011",
  "eui64": "2222",
  "event": "report_freq_set_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "sensor_report_ms": 5000,
    "status_report_ms": 10000,
    "alarm_report_ms": 1000,
    "update_status": "effective"
  }
}
```

### 10\.2 查询当前上报频率 report\_freq\_get

**功能说明**：云端查询设备当前生效的所有主动上报时间间隔参数

**下行指令**

```Plain Text
{
  "reqid": "batch-0012",
  "event": "report_freq_get",
  "data": {}
}
```

**上行响应**

```Plain Text
{
  "reqid": "batch-0012",
  "eui64": "2222",
  "event": "report_freq_get_resp",
  "code": 0,
  "msg": "success",
  "data": {
    "sensor_report_ms": 5000,
    "status_report_ms": 10000,
    "alarm_report_ms": 1000
  }
}
```

---

## 十一、设备主动上报事件（无需下行指令、主动推送）

**上报主题固定**：`otbr/dev/up`

**核心规则**：主动上报为设备自主推送消息，无对应下行指令，无需 reqid；根节点携带 eui64 标识设备，格式统一、可直接用于云端数据解析与设备状态监控。

### 11\.1 传感器数据主动上报 sensor\_report

**设备主动上报报文**

```Plain Text
{
  "eui64": "2222",
  "event": "sensor_report",
  "data": {
    "sensor": "dht22",
    "temp": 25.6,
    "hum": 52.1
  }
}
```

### 


