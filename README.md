# 电力数据采集与监控系统 SCADA

技术栈：C++17 + IEC60870-5-104 + TCP Socket。

本项目面向电网调度自动化场景，模拟主站前置采集程序通过调度数据网接入子站远动装置，完成遥测、遥信、状态监控、告警和本地缓存。工程重点不是简单“收发 socket”，而是按远动系统现场联调思路处理 IEC60870-5-104 链路建立、帧确认、超时、掉线、数据质量和测点异常。

## 项目定位

- 主站/子站架构：`scada_client` 作为主站侧前置采集服务，`iec104_mock_server` 模拟子站 RTU/远动通信装置。
- 调度数据网通信：基于 TCP 2404 端口接入，配置中可切换子站 IP、端口、连接超时、接收超时和重连周期。
- 远动系统模型：按 IOA 建立测点表，支持遥测、遥信、阈值告警、数据质量告警和无刷新告警。
- 现场联调口径：通过模拟子站主动断链、遥测越限、遥信变位和缓存回写，验证主站侧链路稳定性与异常恢复能力。

## 核心功能

- IEC60870-5-104 客户端：支持 STARTDT 启动流程、I 帧接收、S 帧确认、U 帧控制响应。
- 遥测/遥信解析：支持 `M_ME_NC_1` 短浮点遥测、`M_ME_NB_1` 标度遥测、`M_SP_NA_1` 单点遥信。
- 数据采集：将 IOA 映射为测点，输出实时测量值、单位、质量位和采集时间。
- 状态监控：按测点 `stale_seconds` 检查数据新鲜度，发现长时间无刷新自动触发告警。
- 告警功能：支持高高限、高限、低限、低低限、遥信异常、质量异常、数据超时。
- 通信稳定性：链路断开后按 `reconnect_ms` 自动重连，接收超时后主动释放旧连接，避免半开连接长期占用。
- 数据缓存：测量和告警写入 `data/cache.log`，网络异常时本地缓存继续可用，并按最大记录数裁剪。
- 本地模拟：模拟子站周期发送数据，并定时断开连接，用于复现现场调试中的掉线、重连和告警恢复过程。

## 协议解析细节

IEC104 APDU 以 `0x68` 起始，第二字节为长度，后 4 字节为 APCI 控制域，后续为 ASDU。项目中对帧长度、起始字节、控制域格式和 ASDU 类型做了基础校验。

- U 帧：用于链路控制，主站发 `STARTDT act`，子站回 `STARTDT con` 后才进入数据传输状态；收到 `TESTFR act` 时回复 `TESTFR con`。
- I 帧：用于承载 ASDU 数据，解析发送序号和接收序号，提取 TypeID、VSQ、COT、公共地址、IOA 和信息体。
- S 帧：用于确认已接收的 I 帧，主站按收到的 I 帧发送序号递增后回 S 帧确认。
- 遥测解析：`M_ME_NC_1` 按 IEEE754 小端浮点解析，`M_ME_NB_1` 按 16 位标度值解析，并读取 QDS 质量描述。
- 遥信解析：`M_SP_NA_1` 解析 SIQ 的状态位、无效位、非当前位、取代位和闭锁位，避免把遥信 bit0 误判为 QDS 溢出位。

对应实现位于 [src/iec104/Iec104Frame.cpp](<C:/Users/zikang li/Documents/Codex/2026-05-02/scada-c-iec104-socket/src/iec104/Iec104Frame.cpp>) 和 [src/iec104/Iec104Client.cpp](<C:/Users/zikang li/Documents/Codex/2026-05-02/scada-c-iec104-socket/src/iec104/Iec104Client.cpp>)。

## 异常处理

- 掉线：`recv` 返回 0、发送失败或模拟子站主动断开时，主站关闭当前 socket，记录链路离线，并按配置周期重连。
- 超时：连接阶段受 `connect_timeout_ms` 控制，接收 APDU 受 `receive_timeout_ms` 控制，超时后按链路异常处理。
- 数据错乱：接收端持续寻找 `0x68` 起始字节，并校验 APDU 长度；长度非法、帧格式异常或数据不完整时丢弃当前连接，防止错位解析继续扩散。
- 协议不支持：未知 TypeID 不进入测点表，避免把未识别 ASDU 写成错误遥测。
- 质量异常：IEC104 质量位出现 invalid、blocked 等状态时触发数据质量告警。
- 测点无刷新：超过测点配置的 `stale_seconds` 后触发 stale 告警，恢复接收后自动清除。

## 现场联调经验模拟

本地模拟服务端按子站远动装置的行为设计：先等待主站发起 STARTDT，再周期上送遥测和遥信，并可通过 `--drop-every-sec` 主动制造调度数据网闪断。联调时可观察主站侧是否完成重连、是否重复确认 I 帧、遥测越限是否触发告警、遥信变位是否能恢复，以及缓存文件是否持续落盘。

这类流程对应现场常见问题：子站 IP/端口配置不一致、链路能 ping 通但 2404 不通、STARTDT 未确认导致无数据、主站长时间未收到变化数据、远动点表 IOA 与主站模型不一致、质量位异常导致数据可信度下降。

## 工程成果

- 在模拟主站/子站联调环境中完成 IEC60870-5-104 链路建立、遥测遥信采集、告警触发/恢复、主动断链和自动重连验证。
- 按 2,000 点测点规模设计测点映射与本地缓存结构，缓存默认保留 10,000 条测量/告警记录，可按现场容量调整。
- 采集周期按 1 秒级设计，模拟环境下单批遥测遥信从子站发送到主站解析入缓存为毫秒级处理链路。
- 通信可靠性侧重点为“故障可恢复”：掉线后按 2 秒默认周期重连，链路恢复后继续采集，不要求人工重启主站进程。
- 告警链路覆盖越限、遥信异常、质量异常和无刷新，适合支撑调度主站侧的运行监视与问题追溯。

## 构建

```powershell
cmake -S . -B build-make -G "MinGW Makefiles"
cmake --build build-make
```

## 自测

```powershell
.\build-make\scada_selftest.exe
```

## 本地演示

打开两个终端。

终端 1：启动 IEC104 模拟服务端。

```powershell
.\build-make\iec104_mock_server.exe --port 2404 --interval-ms 1000 --drop-every-sec 20
```

终端 2：启动 SCADA 客户端。

```powershell
.\build-make\scada_client.exe --config config\scada.conf
```

运行时可以看到采集值、告警触发/恢复、服务端模拟断线后的客户端自动重连，以及本地缓存文件 `data/cache.log` 的持续写入。

## 配置说明

配置文件位于 `config/scada.conf`，采用 `key=value` 格式。

- `host` / `port`：IEC104 服务端地址。
- `connect_timeout_ms`：TCP 连接超时。
- `receive_timeout_ms`：接收 APDU 超时。
- `reconnect_ms`：断线后的重连间隔。
- `status_interval_ms`：状态监控扫描周期。
- `cache_path`：本地缓存文件。
- `cache_max_records`：缓存最多保留记录数。
- `point.<ioa>.*`：测点名称、类型、单位、阈值、正常遥信状态和超时窗口。

## 目录结构

```text
include/scada/common   配置和日志
include/scada/net      Socket 兼容层和 TCP 客户端
include/scada/iec104   IEC104 帧构造、解析和客户端循环
include/scada/scada    测点、告警、缓存和 SCADA 主流程
include/scada/mock     IEC104 模拟服务端
src                    实现文件
tests                  自测入口
config                 示例配置
```
