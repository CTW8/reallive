# RealLive 测试计划（当前实现）

更新时间：2026-02-15

相关文档：

- 架构：`architecture.md`
- 当前实现快照：`system-understanding.md`
- 运维手册：`manual.md`

## 1. 测试目标

本计划面向当前系统能力，重点验证：

1. 账号与摄像头管理 API 正确性（鉴权、权限隔离、数据一致性）。
2. 实时预览链路稳定（RTMP -> SRS -> HTTP-FLV -> Web/Android）。
3. watch 会话驱动的按需推流行为正确（MQTT 控制闭环）。
4. 历史回看可用（分段、缩略图、时间轴、seek、回到实时）。
5. SEI 数据链路可用（设备资源、每核负载、相机配置、可配置项）。
6. 人物检测事件可用（叠框、事件聚合、时间轴标注、Dashboard 通知）。
7. 性能与稳定性满足家庭监控场景（长时间运行不明显漂移/退化）。

## 2. 测试分层与现状

### 2.1 已具备自动化

- Server Node 测试：`server/test/auth.test.js`, `server/test/cameras.test.js`
- Server 脚本化 smoke：`server/tests/test_api.sh`, `server/tests/test_websocket.js`
- Pusher C++ 单测：`pusher/tests/*`
- Puller C++ 单测：`puller/tests/*`

### 2.2 当前自动化缺口（需补）

- watch/start-heartbeat-stop 的 API 自动化用例。
- history/overview/timeline/play API 自动化用例。
- SEI 解析与事件聚合（server/service 层）自动化用例。
- liveDemand + mqttControl 融合回归测试。
- Web Watch 交互自动化（时间轴缩放/拖拽/seek）。
- Android 端端到端自动化（目前以手工验证为主）。

## 3. 测试环境矩阵

### 3.1 最小联调环境

- 1 台 Linux 主机：Server + SRS + MQTT
- 1 台 Raspberry Pi 5：Pusher（CSI camera）
- 1 台浏览器客户端：Web

### 3.2 完整环境

- Server 节点：Node.js + SQLite
- SRS 节点：SRS v5（8080/1935/1985）
- MQTT：mosquitto
- Edge：Raspberry Pi 5（pusher）
- Client：Web + Android（可选）

### 3.3 版本基线

- Node.js: 18/20
- SRS: 5.x
- CMake: >= 3.16
- GCC: >= 12 (C++17)
- Android: SDK 34, NDK r26+（播放器模块）

## 4. 测试前置条件

1. `server/config/server.json`、`pusher/config/pusher.json`、`server/srs.conf` 已对齐。
2. `stream_key` 在 pusher 与 server camera 记录一致。
3. MQTT topic prefix 一致（默认 `reallive/device`）。
4. pusher 录制目录可写，磁盘空间充足。
5. 系统时间同步（建议 NTP 开启）。

## 5. 自动化执行说明

### 5.1 Server（Node test）

```bash
cd server
npm test
```

单文件执行：

```bash
cd server
node --test test/auth.test.js
node --test test/cameras.test.js
```

### 5.2 Server（脚本 smoke）

默认脚本里常用 `3000`，实际端口不是 3000 时请显式传 URL。

```bash
cd server/tests
bash test_api.sh http://127.0.0.1:80
node test_websocket.js http://127.0.0.1:80
```

### 5.3 Pusher C++ 单测

```bash
cd pusher/tests
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./pusher_tests
```

### 5.4 Puller C++ 单测

```bash
cd puller/tests
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./puller_tests
```

## 6. 功能测试矩阵（核心）

状态字段口径说明（适用于 6.2/6.3）：

- server 常见字段：`desiredLive` / `activeLive`
- MQTT/control payload：`desired_live` / `active_live`
- 二者语义一致，断言时按语义而不是字段风格判断。

### 6.1 账号与摄像头管理

| ID | 场景 | 预期 |
| --- | --- | --- |
| A-01 | 注册/登录成功 | 返回 token 与用户信息 |
| A-02 | 无 token 访问 `/api/cameras` | 401 |
| A-03 | 用户 A 不能操作用户 B camera | 403 |
| A-04 | camera CRUD | 返回码和数据正确 |

### 6.2 实时流与状态同步

| ID | 场景 | 预期 |
| --- | --- | --- |
| L-01 | pusher 推流后 SRS 可见流 | `/api/v1/streams` 出现 stream |
| L-02 | `/api/cameras/:id/stream` | 返回 stream_key/status/srs/device/sei |
| L-03 | Dashboard 状态刷新 | 在线/推流状态与 runtime 一致 |
| L-04 | Socket signaling | 能收到 `camera-status`/`activity-event` |

### 6.3 按需推流（watch + MQTT）

| ID | 场景 | 预期 |
| --- | --- | --- |
| D-01 | Watch 打开触发 start | viewers > 0，desired live=true（`desiredLive` 或 `desired_live`） |
| D-02 | heartbeat 持续 | 会话不超时 |
| D-03 | 关闭 Watch 后等待 grace | 下发停推命令，active live=false（`activeLive` 或 `active_live`） |
| D-04 | MQTT 断连恢复 | 状态可恢复并重新订阅 |
| D-05 | 设备 state 超时(stale) | UI 状态可回退到 SRS/DB 信号，不长期卡死在错误态 |

### 6.4 历史录制与时间轴

| ID | 场景 | 预期 |
| --- | --- | --- |
| H-01 | 录制分段生成 | 出现 `segment_*.mp4` |
| H-02 | 缩略图生成 | 对应 `.jpg` 可访问 |
| H-03 | overview 接口 | hasHistory/range/segmentCount 正确 |
| H-04 | timeline 接口 | segments/events/thumbnails 合理 |
| H-05 | 指定 ts 回放 | `mode=history` 且可播放 |
| H-06 | 回到实时 | 切换 live 成功 |
| H-07 | local-first 回放选择 | local 可播时 `source=local` |
| H-08 | 强制 edge 回放 | `mode=edge` 时返回 `source=edge`（edge 启用场景） |
| H-09 | edge 回放停止 | 调用 `history/replay/stop` 后可回到 live 且无残留 session |

### 6.5 SEI 与图表

| ID | 场景 | 预期 |
| --- | --- | --- |
| S-01 | 设备遥测 SEI | cpu/mem/storage 数据持续刷新 |
| S-02 | 每核负载 SEI | core 曲线可展示 |
| S-03 | camera/configurable SEI | 卡片字段展示正确 |
| S-04 | SEI 中断 | UI 显示离线/等待状态 |

### 6.6 人物检测事件

| ID | 场景 | 预期 |
| --- | --- | --- |
| P-01 | 画面有人 | 出现框，score 合理 |
| P-02 | 单人持续在场 | 事件应聚合，不应高频刷屏 |
| P-03 | 无人场景 | 不应出现持续假事件 |
| P-04 | 时间轴事件标记 | marker 与事件列表一致 |
| P-05 | Dashboard 事件通知 | unread/last event 正确变化 |

### 6.7 Android 客户端

| ID | 场景 | 预期 |
| --- | --- | --- |
| M-01 | app 拉起 dashboard/watch | camera 列表与 watch 可用 |
| M-02 | 实时播放 | 画面连续、无明显卡死 |
| M-03 | 历史播放 + seek | 指定时间可播、可回 live |
| M-04 | Surface 生命周期 | 前后台切换不崩溃、不黑屏锁死 |

## 7. 端到端回归场景（推荐每次发布前）

### 场景 R1：基础链路

1. 启动 MQTT/SRS/Server/Pusher。
2. 登录 Web，进入 Watch。
3. 确认实时画面、SEI 卡片、资源图可用。
4. 停留 5 分钟无报错。

通过标准：

- FPS 稳定，未出现持续掉帧或断流。
- Server/SRS 无持续错误日志。

### 场景 R2：历史闭环

1. 持续录制 10 分钟。
2. Watch 查看时间轴缩略图与事件标记。
3. 拖动 seek 到 3 个时间点播放。
4. 点击 Back To Live 返回实时。

通过标准：

- 3 个时间点均有画面。
- 缩略图和事件位置与实际内容一致。

### 场景 R3：按需开关流

1. 所有 Watch 关闭，观察 grace 周期后停推。
2. 再次打开 Watch，恢复推流。

通过标准：

- 停推/恢复均在可接受时间内完成。
- Dashboard 状态随 runtime 变化。

## 8. 性能与稳定性指标

### 8.1 建议指标

| 指标 | 目标 |
| --- | --- |
| 实时主链路 FPS | 30fps（720p）稳定 |
| pusher AvgProcess | ~33ms 级别（参考当前已优化基线） |
| Encode 耗时 | 单帧约 3~8ms |
| RTMP send | 平均 < 1ms |
| E2E 延迟 | 持续运行不明显增长 |
| 进程稳定性 | 24h 无崩溃/死锁 |

### 8.2 Soak Test（建议）

- 持续运行 24 小时：
  - 每 10 分钟记录一次 FPS、延时、CPU/内存。
  - 每 2 小时执行一次 history seek。
  - 至少模拟 3 次 watch 打开/关闭循环。

## 9. 发布准入（Release Gate）

满足以下条件方可发布：

1. Server 自动化测试全绿（`npm test`）。
2. Pusher/Puller 单测全绿。
3. 回归场景 R1/R2/R3 全通过。
4. 无 P0/P1 未修复缺陷。
5. 文档配置与默认配置文件一致。

## 10. 缺陷分级

- P0：服务不可用、数据损坏、严重安全问题。
- P1：核心功能不可用（实时/回看/按需控制失败）。
- P2：功能可用但体验明显受损（错位、偶发失败、延时抖动）。
- P3：UI 细节或低影响问题。

处理原则：

- P0/P1 必须阻塞发布。
- P2 需评估风险并给出回归计划。
- P3 可并入后续迭代。

## 11. 后续自动化建设建议

优先级从高到低：

1. 新增 server API 测试：watch/history/sei 相关接口。
2. 为 `liveDemandService`、`mqttControlService` 增加 service 级单测。
3. 增加 Web E2E（Playwright）覆盖时间轴 seek/缩放/Back To Live。
4. 增加 Android 仪表化测试覆盖 Surface 生命周期和播放稳定性。
5. 增加 2h/24h soak 自动采样脚本并固化报告模板。
