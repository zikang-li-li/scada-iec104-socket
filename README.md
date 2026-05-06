# 电力 SCADA IEC104 多子站通信调试与仿真系统

## 项目概述

本项目是一个基于 C++17 和 TCP Socket 实现的 IEC60870-5-104 通信调试与仿真系统，面向电力调度自动化、变电站远动接入、配网通信联调和数据网故障排查场景。

系统模拟主站前置采集程序与多个 RTU / 远动装置之间的 IEC104 通信过程，支持多设备接入、遥测遥信处理、链路心跳、断线重连、模拟子站、协议探测、数据缓存和工程日志输出。项目重点不在算法复杂度，而在贴近电力现场通信系统的可靠性、可观测性和联调排障能力。

## 业务背景

在电力 SCADA / 调度自动化系统中，主站通常通过调度数据网接入多个子站 RTU、测控装置或通信管理机。现场联调中经常遇到以下问题：

- 子站 IP、端口、路由、防火墙或安全隔离配置异常，导致 TCP/2404 不通。
- TCP 已连接，但 IEC104 `STARTDT` 启动流程未完成，主站收不到业务数据。
- 点表 IOA、公共地址、遥测遥信类型与主站模型不一致。
- 链路闪断、光纤收发器重启、交换机抖动导致采集间歇中断。
- 子站长时间不上送数据，但 TCP 连接未立即断开。
- 遥测越限、遥信变位、质量位异常需要被准确记录和追溯。

本项目围绕这些现场问题设计，用于搭建本地可复现的电力通信调试环境。

## 系统能力

### IEC104 主站采集

- 支持 IEC104 TCP 客户端连接，默认端口 `2404`。
- 支持 `STARTDT act/con` 启动流程。
- 支持 I 帧接收、S 帧确认和 U 帧控制响应。
- 支持 `TESTFR act/con` 心跳检测。
- 支持链路断开后的自动重连，重连周期可配置。
- 支持多 RTU / 多子站并发接入，每个设备独立连接、独立心跳、独立重连。

### 远动数据处理

- 区分遥测、遥信、遥控业务类型。
- 遥测支持短浮点值 `M_ME_NC_1`、标度值 `M_ME_NB_1`。
- 遥信支持单点信息 `M_SP_NA_1`。
- 支持按设备名输出业务日志，例如：

```text
[INFO] Telemetry [110kV East Substation RTU] 110kV bus voltage=222.01 kV IOA=1002 quality=good
[INFO] Telesignal [110kV East Substation RTU] 110kV breaker QF1=CLOSED IOA=2001 quality=good
```

### 通信稳定性设计

- 自动检测连接失败、接收失败、发送确认失败和心跳超时。
- 链路异常后主动关闭旧 socket，进入定时重连流程。
- 心跳机制采用 IEC104 标准 `TESTFR act/con`。
- 多设备模式下，单个 RTU 掉线不影响其他 RTU 正常采集。

### 模拟子站

项目内置 `iec104_mock_server`，用于模拟现场 RTU / 远动装置：

- 周期性上送遥测、遥信。
- 支持越限数据模拟。
- 支持质量位异常模拟。
- 支持遥信分合变位模拟。
- 支持 TCP 保持但停止上送数据，用于验证心跳和无刷新告警。
- 支持主动断链，用于验证主站重连能力。

### 协议探测与排障

`iec104_probe` 可用于快速判断 IEC104 站端通信状态：

- TCP/2404 是否可连接。
- `STARTDT` 是否确认。
- I/S/U 帧是否正常。
- TypeID、COT、公共地址、IOA 是否符合点表。
- 遥测遥信质量位是否异常。
- 可输出十六进制 APDU，便于与抓包结果对照。

### 数据缓存与日志

- 支持线程安全的内存最新值缓存。
- 支持本地文件缓存测量值、业务记录和告警事件。
- 支持工程日志输出到控制台和文件。
- 支持 `INFO`、`WARN`、`ERROR`、`DEBUG` 日志等级。
- 日志带毫秒级时间戳，便于排查链路抖动和数据刷新时间。

## 程序组成

| 程序 | 说明 |
| --- | --- |
| `scada_client` | 主站侧 IEC104 采集程序，读取多设备配置并建立多条 IEC104 链路 |
| `iec104_mock_server` | 模拟 RTU / 子站远动装置，用于联调和故障注入 |
| `iec104_probe` | IEC104 链路探测工具，用于快速排查 IP、端口、STARTDT、IOA 和质量位 |
| `scada_cache_report` | 本地缓存报告工具，用于查看最新测点和当前未恢复告警 |
| `scada_selftest` | 协议解析、配置解析、日志和缓存基础自测 |

## 主要模块

```text
include/scada/common   配置解析与工程日志
include/scada/net      跨平台 TCP Socket 封装
include/scada/iec104   IEC104 APDU 构造、解析、客户端链路
include/scada/scada    测点模型、业务数据、告警、缓存、主站调度
include/scada/mock     IEC104 模拟子站
src/tools              协议探测和缓存报告工具
config                 多 RTU 示例配置
docs                   现场调试说明
tests                  自测入口
```

## 支持的现场场景

### 多子站接入

主站从 `config/scada.conf` 读取多个 `device`，每个设备拥有独立 IP、端口、公共地址、心跳参数、重连参数和点表。

适用场景：

- 多个变电站 RTU 接入同一主站前置。
- 多台通信管理机并行调试。
- 同一个 IOA 在不同站点复用，但业务上需要按设备区分。

### 链路稳定性验证

适用场景：

- 调度数据网链路抖动。
- 子站重启导致 TCP 断开。
- TCP 半开连接导致业务数据长时间不上送。
- 主站需要自动恢复采集，不依赖人工重启程序。

### 点表与协议排障

适用场景：

- 核对现场远动点表 IOA。
- 判断 TypeID 是否符合遥测/遥信定义。
- 判断公共地址是否配置一致。
- 对照抓包分析 IEC104 APDU。
- 快速定位“网络通但主站无数据”的问题。

### 告警与数据追溯

适用场景：

- 遥测越限。
- 遥信异常变位。
- 数据质量位 invalid / not topical / blocked。
- 测点长时间无刷新。
- 事后通过缓存文件和日志追溯故障时间线。

## 构建方式

```powershell
cmake -S . -B build-make -G "MinGW Makefiles"
cmake --build build-make
```

## 运行示例

启动两个模拟子站：

```powershell
.\build-make\iec104_mock_server.exe --port 2404 --scenario mixed --interval-ms 1000 --drop-every-sec 20
```

```powershell
.\build-make\iec104_mock_server.exe --port 2405 --scenario quality --interval-ms 1000 --drop-every-sec 0
```

启动主站采集：

```powershell
.\build-make\scada_client.exe --config config\scada.conf
```

开启协议调试日志：

```powershell
.\build-make\scada_client.exe --config config\scada.conf --debug
```

探测单个子站：

```powershell
.\build-make\iec104_probe.exe --host 127.0.0.1 --port 2404 --seconds 8 --hex
```

查看缓存报告：

```powershell
.\build-make\scada_cache_report.exe --cache data\cache.log
```

## 多设备配置示例

`config/scada.conf` 使用 `key=value` 格式，适合现场快速修改：

```ini
status_interval_ms=1000
cache_path=data/cache.log
cache_max_records=10000
log_path=logs/scada_client.log
log_level=info
log_console=true
log_append=true

device.rtu_110kv.name=110kV East Substation RTU
device.rtu_110kv.enabled=true
device.rtu_110kv.host=127.0.0.1
device.rtu_110kv.port=2404
device.rtu_110kv.common_address=1
device.rtu_110kv.connect_timeout_ms=3000
device.rtu_110kv.receive_timeout_ms=5000
device.rtu_110kv.reconnect_ms=3000
device.rtu_110kv.heartbeat_interval_ms=10000
device.rtu_110kv.heartbeat_timeout_ms=3000

device.rtu_110kv.point.1001.name=110kV main transformer load
device.rtu_110kv.point.1001.type=analog
device.rtu_110kv.point.1001.unit=MW
device.rtu_110kv.point.1001.high_high=95
device.rtu_110kv.point.1001.high=85
device.rtu_110kv.point.1001.stale_seconds=15

device.rtu_110kv.point.2001.name=110kV breaker QF1
device.rtu_110kv.point.2001.type=digital
device.rtu_110kv.point.2001.normal_state=1
device.rtu_110kv.point.2001.stale_seconds=20

device.rtu_west.name=110kV West Substation RTU
device.rtu_west.enabled=true
device.rtu_west.host=127.0.0.1
device.rtu_west.port=2405
device.rtu_west.common_address=1
device.rtu_west.reconnect_ms=3000
device.rtu_west.heartbeat_interval_ms=10000
device.rtu_west.heartbeat_timeout_ms=3000

device.rtu_west.point.1001.name=West transformer load
device.rtu_west.point.1001.type=analog
device.rtu_west.point.1001.unit=MW
```

## 模拟故障场景

```powershell
# 正常遥测遥信上送
.\build-make\iec104_mock_server.exe --scenario normal

# 遥测越限
.\build-make\iec104_mock_server.exe --scenario alarm

# 质量位异常
.\build-make\iec104_mock_server.exe --scenario quality --quality-every 3

# 遥信分合变化
.\build-make\iec104_mock_server.exe --scenario digital-trip

# TCP 不断开，但停止上送遥测，用于验证心跳和无刷新告警
.\build-make\iec104_mock_server.exe --scenario stale --quiet-after-sec 8 --drop-every-sec 0

# 主动断链，用于验证主站自动重连
.\build-make\iec104_mock_server.exe --scenario mixed --drop-every-sec 20
```

## 典型日志

```text
[2026-05-03 21:26:39.102] [INFO] IEC104 TCP connected: 127.0.0.1:2404
[2026-05-03 21:26:39.105] [INFO] IEC104 data transfer started: 127.0.0.1:2404
[2026-05-03 21:26:39.218] [INFO] Telemetry [110kV East Substation RTU] 110kV bus voltage=222.01 kV IOA=1002 quality=good
[2026-05-03 21:26:39.220] [INFO] Telesignal [110kV East Substation RTU] 110kV breaker QF1=CLOSED IOA=2001 quality=good
[2026-05-03 21:26:43.001] [INFO] IEC104 heartbeat TX: 127.0.0.1:2404
[2026-05-03 21:26:43.005] [INFO] IEC104 heartbeat OK: 127.0.0.1:2404
[2026-05-03 21:26:49.983] [WARN] IEC104 connect failed: 127.0.0.1:2405; reconnecting in 3000 ms
```

## 工程特点

- 使用 C++17 实现，核心逻辑不依赖大型第三方框架。
- 网络层、协议层、业务层、模拟层职责分离。
- 多 RTU 接入按设备隔离，便于扩展真实站点配置。
- 心跳、重连、日志、缓存和告警均围绕现场稳定性设计。
- 工具链覆盖采集、模拟、探测、排障和报告，适合作为电力通信岗位项目展示。
