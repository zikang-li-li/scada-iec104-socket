# IEC104 现场调试演示手册

## 1. 先判断链路是不是通

```powershell
.\build-make\iec104_probe.exe --host 127.0.0.1 --port 2404 --seconds 5
```

看三件事：

- 能否 TCP connect 到 2404。
- 是否收到 `STARTDT con`。
- 是否持续收到 I 帧，且 TypeID/COA/IOA 与点表一致。

## 2. 看协议帧是否能解释成业务点

打开十六进制输出：

```powershell
.\build-make\iec104_probe.exe --seconds 8 --hex
```

输出里应能看到类似：

```text
I s=0 r=0 type=13(M_ME_NC_1) cot=3(spontaneous) ca=1 count=1 | IOA=1001 value=74.12 q=good
```

面试时可以解释：

- `type=13` 是短浮点遥测 `M_ME_NC_1`。
- `cot=3` 表示突发/自发上送。
- `ca=1` 是公共地址。
- `IOA=1001` 对应点表里的主变负荷。
- `q=good/invalid/not_topical/blocked` 是现场判断数据可信度的重要依据。

## 3. 注入典型故障

越限告警：

```powershell
.\build-make\iec104_mock_server.exe --scenario alarm
```

质量位异常：

```powershell
.\build-make\iec104_mock_server.exe --scenario quality --quality-every 3
```

遥信异常：

```powershell
.\build-make\iec104_mock_server.exe --scenario digital-trip
```

链路不断但数据无刷新：

```powershell
.\build-make\iec104_mock_server.exe --scenario stale --quiet-after-sec 8 --drop-every-sec 0
```

链路闪断重连：

```powershell
.\build-make\iec104_mock_server.exe --scenario mixed --drop-every-sec 20
```

## 4. 主站侧验证

```powershell
.\build-make\scada_client.exe --config config\scada.conf --debug
```

重点看：

- 主站发送 `STARTDT act`，子站返回 `STARTDT con`。
- 主站收到 I 帧后返回 S 帧确认。
- 链路空闲超过 `heartbeat_interval_ms` 后发送 `TESTFR act`，收到 `TESTFR con` 后输出 heartbeat OK。
- `heartbeat_timeout_ms` 内没有心跳响应时，主站主动判定掉线并进入 `reconnect_ms` 重连。
- 测点值进入缓存。
- 越限、质量位、遥信异常、无刷新是否触发告警。
- 断链后是否按 `reconnect_ms` 自动重连。

心跳验证可以使用“TCP 不断但不再上送遥测”的场景：

```powershell
.\build-make\iec104_mock_server.exe --scenario stale --quiet-after-sec 1 --drop-every-sec 0
```

主站日志应出现：

```text
IEC104 heartbeat TX: 127.0.0.1:2404
IEC104 heartbeat OK: 127.0.0.1:2404
```

## 5. 事后追溯

```powershell
.\build-make\scada_cache_report.exe --cache data\cache.log
```

报告会汇总：

- 缓存总记录数。
- 测量记录数和告警记录数。
- 每个 IOA 的最新测值、IEC 类型和质量状态。
- 当前仍未恢复的告警。

这个流程对应技术支持岗位常见工作：先判网络，再判协议，再判点表，再判数据质量，最后给出可追溯日志。
