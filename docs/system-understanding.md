# RealLive 当前系统理解（实现快照）

更新时间：2026-02-15

## 1. 系统目标

RealLive 当前目标是一个“家用监控闭环系统”：

- 设备端（Pusher）采集/编码/推流，支持本地分段录制与人物检测。
- 媒体层（SRS）负责 RTMP ingest 与 HTTP-FLV 分发。
- 服务端（Server）负责鉴权、设备管理、状态聚合、回看编排、按需开关流控制。
- 前端（Web / Android）负责实时预览、历史回看、事件查看、资源监控展示。

核心设计原则：

- 媒体面与控制面解耦（RTMP/FLV 与 REST/MQTT/WS 分离）。
- 直播按需（无人观看时自动停推，降低设备负载和带宽占用）。
- 事件与遥测统一通过 SEI 上报，服务端解析后统一对外。

## 2. 组件与职责

| 组件 | 技术栈 | 主要职责 |
| --- | --- | --- |
| `pusher` | C++ (libcamera/FFmpeg/OpenCV/TFLite/MQTT) | 采集、编码、RTMP 推流、本地录制、人物检测、SEI 注入、MQTT 运行时控制 |
| `SRS` | SRS v5 | 接收 RTMP、输出 HTTP-FLV、提供流状态 API |
| `server` | Node.js/Express/SQLite/Socket.io | 用户与相机 API、SRS 同步、SEI 解析聚合、历史时间轴 API、watch 会话与按需开流、MQTT 控制桥接 |
| `server/web` | Vue3 + mpegts.js + ECharts | Dashboard、Watch 页面、实时预览、时间轴回看、事件标注、SEI 资源图/配置卡 |
| `android` | Kotlin + JNI + C++ FFmpeg/GLES | 原生播放端（HTTP-FLV），实时/历史播放原型 |
| `puller` | C++ | 历史相关辅助链路（当前主回看路径以 pusher 本地录制 + server 聚合为主） |

## 3. 关键链路

### 3.1 实时直播链路（媒体面）

1. `pusher` 采集 CSI 相机帧，编码 H.264。
2. 关键帧周期注入 SEI（设备遥测、相机配置、检测状态/事件）。
3. 通过 RTMP 推送到 SRS：`rtmp://<srs>/live/<stream_key>`。
4. Web/Android 从 server 侧代理地址播放 FLV：`/live/<stream_key>.flv`。
5. server 通过反向代理转发到 SRS（`http://localhost:8080/live/...`）。

### 3.2 直播按需链路（控制面）

1. 用户进入 Watch：`POST /api/cameras/:id/watch/start`。
2. 前端每 10s 心跳：`POST /watch/heartbeat`。
3. 退出/关闭页面：`POST /watch/stop` 或超时淘汰。
4. `liveDemandService` 根据会话数计算 `desiredLive`。
5. 优先通过 `mqttControlService` 下发设备命令（MQTT）；若不可用可走 `edgeReplayService` fallback。
6. 设备端 `MqttRuntimeClient` 收到命令后调用 `Pipeline::setLivePushEnabled()` 开/停推流。

默认时序参数（可环境变量覆盖）：

- `WATCH_HEARTBEAT_MS` 默认 10000ms
- `WATCH_SESSION_TTL_MS` 默认 25000ms
- `WATCH_LIVE_STOP_GRACE_MS` 默认 30000ms

### 3.3 按需推流时序（简版）

```text
watch/start -> desiredLive=true -> MQTT live:on -> pusher active_live=true -> SRS 出现 live 流
heartbeat   -> 刷新会话 TTL
watch/stop  -> 等待 grace -> desiredLive=false -> MQTT live:off -> pusher active_live=false
```

说明：

- `desired` 是目标态，`active` 是实际态，短暂不一致是正常过渡。
- 超过 `stateStaleMs` 未收到设备状态时，server 会降低 MQTT 状态权重，回退参考 SRS/DB。

### 3.4 历史回看链路

1. `pusher` 本地按段录制 MP4：`segment_<start>_<end>.mp4`。
2. 同时生成缩略图：`segment_<start>_<end>.jpg`。
3. 检测事件写入 `events.ndjson`。
4. `server/historyService` 扫描录制目录，聚合：
   - 概览：`/history/overview`
   - 时间轴：`/history/timeline`
   - 指定时刻播放定位：`/history/play?ts=...`
5. Watch 页面基于时间轴拖动/缩放并切换到历史回看；支持“Back To Live”回实时。

### 3.5 历史回看时序（简版）

```text
watch 拉 timeline -> server 优先走 local historyService
seek(ts)          -> /history/play 返回 source + playbackUrl + offsetSec
若 local 不可播   -> 自动/强制切 edgeReplay
Back To Live      -> (可选) /history/replay/stop -> 切回 /live/<stream_key>.flv
```

补充：

- `/history/play` 默认 local-first，只有 local 不可满足时才回退 edge（或显式 `mode=edge` 强制）。
- open 段（正在写入）通常不可直接 seek 播放，会选最近可播 segment。

## 4. pusher 当前内部架构

`Pipeline` 当前是“多线程分工 + 检测异步”的结构：

- `captureThread`：拉取相机帧。
- `sendThread`：叠框/叠字、编码、RTMP 发送、录制写盘、SEI 注入。
- `detectThread`：独立执行运动检测 + TFLite 检测，不阻塞主发送路径。
- `audioThread`（可选）：音频采集发送。

检测策略为“两阶段”思路：

- 第一阶段：OpenCV/轻量运动检测快速门控。
- 第二阶段：TFLite 模型推理（当前配置为 YOLOv8n TFLite）。
- 输出：
  - 实时叠框（可开关 `detect_draw_overlay`）。
  - 人物事件（节流写入 `events.ndjson`）。
  - SEI 内 `person` 与 `events` 字段上报。

## 5. SEI 数据模型（设备 -> server）

SEI user-data payload（JSON）包含：

- `device`: CPU/内存/存储整体占用 + 每核心负载
- `camera`: 当前相机/编码配置
- `configurable`: 可配置项范围（分辨率、fps、码率、gop 等）
- `person`: 当前跟踪人物框与分数
- `events`: 检测事件列表（`person_detected`）

server 侧 `seiMonitor` 从 FLV/H.264 解析 SEI，输出给 API：

- `streamInfo.sei.telemetry`
- `streamInfo.sei.telemetryHistory`
- `streamInfo.sei.cameraConfig`
- `streamInfo.sei.configurable`
- `streamInfo.sei.person`
- `streamInfo.sei.personEvents`

## 6. MQTT 控制模型

### 6.1 Topic 规范

- 命令：`<topicPrefix>/<stream_key>/command`
- 状态：`<topicPrefix>/<stream_key>/state`

默认 `topicPrefix = reallive/device`。

### 6.2 命令语义

服务端发布：

- `type = live`
- `enable = true/false`
- `seq` 用于去重与乱序保护

设备端执行后上报状态：

- `running`
- `desired_live`
- `active_live`
- `command_seq`
- `reason`

server 基于状态新鲜度（`stateStaleMs`）推导 camera 在线/离线/推流中状态。

### 6.3 字段命名映射（统一口径）

为避免跨组件命名不一致，统一按“概念 + 字段名映射”理解：

- 目标推流状态（desired live）：
  - server 内部：`desiredLive`
  - MQTT/control payload：`desired_live`
- 实际推流状态（active live）：
  - server 内部：`activeLive`
  - MQTT/control payload：`active_live`
- 进程运行状态（running）：
  - server 与 MQTT 均为 `running`

说明：

- `desired live` 表示 server 根据 watch 会话计算出的目标状态。
- `active live` 表示设备当前是否真的在发送 live 媒体。
- `running` 仅表示 pipeline 存活，不等同于正在推流。

## 7. 存储与保留策略

录制目录结构（以 `pusher/config/pusher.json` 为准）：

- `record_output_dir/<stream_key>/segment_<start>_<end>.mp4`
- `record_output_dir/<stream_key>/segment_<start>_<end>.jpg`
- `record_output_dir/<stream_key>/events.ndjson`

滚动清理策略：

- 当磁盘可用低于 `record_min_free_percent`（默认 15%）开始删除最旧分段。
- 删除直到达到 `record_target_free_percent`（默认 20%）或无法继续删除。

## 8. Web 当前能力（Watch/Dashboard）

Watch 页面：

- 实时预览（mpegts.js 播放 HTTP-FLV）。
- 历史时间轴：缩略图轨道、刻度、事件 marker、拖动 seek、滚轮缩放、Back To Live。
- 事件列表：人物事件聚合展示。
- 资源图：ECharts 绘制 CPU/内存/存储曲线 + CPU 每核心曲线。
- 配置卡：展示 `cameraConfig` 与 `configurable`。

Dashboard 页面：

- 相机在线/推流状态。
- 最近缩略图预览。
- 事件通知与活动流。

## 9. Android 当前能力（独立原生播放器）

Android 已建立分层：

- Kotlin 接口层：`Player`
- Kotlin JNI 实现：`NativePlayer`
- JNI 桥：`NativePlayerJni.cpp`
- Native 接口/控制层：`IPlayer` + `PlayerController`
- Native 实现：`FfmpegPlayer`

播放链路：

- `avformat` demux HTTP-FLV
- `avcodec` 解码
- `swscale` 转 RGBA
- OpenGL ES 渲染到 `SurfaceView`

## 10. server 核心服务清单

- `srsSync`: 轮询 SRS API，同步流状态。
- `seiMonitor`: 拉取 FLV，解析 SEI，缓存遥测/事件。
- `historyService`: 聚合本地录制段、缩略图、事件与回放定位。
- `liveDemandService`: 管理 watch 会话、心跳、按需开关流。
- `mqttControlService`: MQTT 命令下发与设备状态接入。
- `edgeReplayService`: 边缘回放/运行时控制 fallback 适配层。

## 11. 关键配置入口

- Server: `server/config/server.json`
- Pusher: `pusher/config/pusher.json`
- SRS: `server/srs.conf`
- Android Native Player 架构说明: `android/player-native/ARCHITECTURE.md`

## 12. 现阶段边界与说明

- 当前实时与回看主协议均为 HTTP-FLV（非 WebRTC）。
- server 对外提供统一 API，但历史数据来源可在 `local` 与 `edge` 间切换。
- Dashboard/Watch 展示状态是“DB 状态 + MQTT 运行态 + SRS/SEI 数据”的融合结果，不是单点真值。
- 文档以当前仓库实现为准，后续若改协议或目录结构请同步更新本文件。
