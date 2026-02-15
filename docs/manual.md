# RealLive 编译、部署与运行手册（当前实现）

更新时间：2026-02-15

相关文档：

- 架构：`architecture.md`
- 当前实现快照：`system-understanding.md`
- 测试计划：`test-plan.md`

## 1. 适用范围

本文档面向当前仓库实现，覆盖：

- `server`（Node.js API + Web UI + signaling）
- `pusher`（Raspberry Pi 推流/检测/录制/MQTT 控制）
- `SRS`（RTMP ingest + HTTP-FLV output）
- `android`（原生 HTTP-FLV 播放原型）
- `puller`（辅助模块，非当前主回看链路）

## 2. 当前链路摘要

- 实时：`pusher -> RTMP -> SRS -> HTTP-FLV -> server 代理 -> Web/Android`
- 历史：`pusher 本地录制(mp4+jpg+events.ndjson) -> server/historyService -> watch 时间轴`
- 控制：`watch 会话 -> liveDemandService -> mqttControlService -> pusher MqttRuntimeClient`

## 3. 端口与协议

| 模块 | 端口 | 协议 | 用途 |
| --- | --- | --- | --- |
| SRS | 1935 | RTMP | 接收 pusher 推流 |
| SRS | 8080 | HTTP | 输出 live/history FLV |
| SRS | 1985 | HTTP API | 流状态查询 |
| Server | 80（默认配置） | HTTP/WS | REST API + Web UI + `/ws/signaling` |
| MQTT Broker | 1883 | MQTT | 下发 live 开关命令，回收设备状态 |
| Pusher ControlServer（可选） | 8090 | HTTP | 设备本地回放/运行时控制接口 |

说明：

- 当前建议 server 使用 `server/config/server.json` 配置，不依赖启动命令行环境变量。
- server 会把 `/live/*` 和 `/history/*` 反向代理到 `http://localhost:8080`（SRS）。

## 4. 依赖安装

## 4.1 Server 节点

```bash
sudo apt update
sudo apt install -y nodejs npm
```

## 4.2 Pusher 节点（Raspberry Pi 5）

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libcamera-dev \
  libavformat-dev libavcodec-dev libavutil-dev \
  libasound2-dev \
  libopencv-core-dev libopencv-imgproc-dev \
  libmosquitto-dev
```

可选（启用 TFLite）：

```bash
sudo apt install -y libtensorflow-lite-dev
```

## 4.3 MQTT Broker

```bash
sudo apt update
sudo apt install -y mosquitto
sudo systemctl enable --now mosquitto
sudo systemctl status mosquitto
```

## 5. 配置文件

## 5.1 Server 配置：`server/config/server.json`

当前关键字段：

- `port`: server HTTP 端口
- `srsApi`: SRS API 地址（默认 `http://localhost:1985`）
- `edgeReplay`: 可选 edge 回放适配
- `mqttControl`: server 控制设备开关流配置

示例（与当前代码兼容）：

```json
{
  "port": 80,
  "dbPath": "./data/reallive.db",
  "srsApi": "http://localhost:1985",
  "edgeReplay": {
    "url": "http://127.0.0.1:8090",
    "timeoutMs": 1500
  },
  "mqttControl": {
    "enabled": true,
    "brokerUrl": "mqtt://127.0.0.1:1883",
    "topicPrefix": "reallive/device",
    "commandQos": 1,
    "stateQos": 0,
    "commandRetain": true,
    "stateStaleMs": 12000
  }
}
```

## 5.2 Pusher 配置：`pusher/config/pusher.json`

当前关键字段：

- 推流：`url`, `stream_key`, `width`, `height`, `fps`, `bitrate`, `gop`
- 本地录制：`enable_record`, `record_output_dir`, `record_segment_seconds`
- 存储清理：`record_min_free_percent`, `record_target_free_percent`
- 本地控制：`control_enable`, `control_port`, `replay_rtmp_base`
- MQTT：`mqtt_enable`, `mqtt_host`, `mqtt_port`, `mqtt_topic_prefix`
- 检测：`detect_*`, `detect_tflite_model`

示例（节选）：

```json
{
  "url": "rtmp://localhost:1935/live",
  "stream_key": "598c05a2-8386-4dac-8711-2ec9a9b3b2f7",
  "width": 1280,
  "height": 720,
  "fps": 30,
  "gop": 15,
  "bitrate": 2000000,

  "enable_record": true,
  "record_output_dir": "./recordings",
  "record_segment_seconds": 60,
  "record_min_free_percent": 15,
  "record_target_free_percent": 20,
  "record_thumbnail": true,

  "control_enable": true,
  "control_host": "0.0.0.0",
  "control_port": 8090,
  "replay_rtmp_base": "rtmp://localhost:1935/history",

  "mqtt_enable": true,
  "mqtt_host": "127.0.0.1",
  "mqtt_port": 1883,
  "mqtt_topic_prefix": "reallive/device",

  "detect_enable": true,
  "detect_draw_overlay": true,
  "detect_tflite_enable": true,
  "detect_tflite_model": "/path/to/reallive/model/yolov8n_float16.tflite"
}
```

## 5.3 SRS 配置：`server/srs.conf`

当前仓库配置特征：

- 低延迟参数已开启（`min_latency on`, `gop_cache off`）
- HTTP server 端口 `8080`
- API 端口 `1985`

## 6. 编译

## 6.1 Server + Web

```bash
cd server
npm install

cd web
npm install
npm run build
```

## 6.2 Pusher

```bash
cd pusher
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

若缺失依赖，CMake 日志会提示 `OpenCV/TFLite/MQTT` 对应能力是否启用。

## 6.3 Puller（可选）

```bash
cd puller
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

说明：puller 仍可用于辅助录制/实验，但不是当前 watch 时间轴主数据来源。

## 7. 启动顺序（推荐）

## 7.1 启动 MQTT

```bash
sudo systemctl start mosquitto
```

## 7.2 启动 SRS

方式 A（Docker）：

```bash
docker run -d --name srs --restart unless-stopped \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 \
  -v /path/to/reallive/server/srs.conf:/usr/local/srs/conf/srs.conf \
  ossrs/srs:5 ./objs/srs -c conf/srs.conf
```

方式 B（本机二进制）：

```bash
/path/to/srs -c /path/to/reallive/server/srs.conf
```

## 7.3 启动 Server

```bash
cd server
npm run dev
```

说明：若 `server.json` 中端口为 `80`，Linux 下通常需要 root 权限或端口能力；开发环境可改为 `3000`/`8088` 等非特权端口。

## 7.4 启动 Pusher

```bash
cd pusher
./build/reallive-pusher -c config/pusher.json
```

## 7.5 启动 Web（开发模式，可选）

如果只使用 server 内置静态页面（`web/dist`）可不单独启动。

开发调试前端时：

```bash
cd server/web
npm run dev
```

## 8. 启动后自检清单

## 8.1 SRS

```bash
curl -s http://127.0.0.1:1985/api/v1/versions
curl -s http://127.0.0.1:1985/api/v1/streams
```

## 8.2 Server

```bash
curl -s http://127.0.0.1:80/api/health
```

预期看到 `status: ok`。

## 8.3 MQTT 控制链路

- Server 日志应出现：`[MQTT Control] Connected ... subscribed .../state`
- Pusher 日志应出现：`[MQTT] Runtime control started`

## 8.4 Camera 状态链路

- Dashboard 页面进入后可见 camera 卡片状态。
- Watch 页面打开后，`/watch/start` 成功，设备应进入目标推流状态（`desiredLive=true` 或 `desired_live=true`）。
- 关闭 Watch 后，经过 grace 时间（默认 30s）应自动停止 live push。

## 9. 运行时行为说明

## 9.1 按需推流

- watch 有会话：设备保持 live push。
- watch 全部离开：达到 `WATCH_LIVE_STOP_GRACE_MS` 后 server 下发停推命令。
- 设备状态通过 MQTT `.../state` 回传，dashboard 显示在线/推流中。

## 9.2 历史录制与回看

- pusher 会把录像分段写入 `record_output_dir/<stream_key>/`。
- 生成文件：
  - `segment_<start>_<end>.mp4`
  - `segment_<start>_<end>.jpg`
  - `events.ndjson`
- server `historyService` 扫描这些文件并生成时间轴和播放定位。

## 9.3 存储自动清理

- 当磁盘空闲 < `record_min_free_percent`（默认 15%）开始删最旧分段。
- 删除直到空闲达到 `record_target_free_percent`（默认 20%）。

## 9.4 运行态字段口径（联调必看）

在日志、接口和 MQTT 抓包中会同时看到 camelCase 与 snake_case，语义一致：

- 目标推流状态（desired live）：
  - server 侧常见：`desiredLive`
  - MQTT/control payload：`desired_live`
- 实际推流状态（active live）：
  - server 侧常见：`activeLive`
  - MQTT/control payload：`active_live`
- 进程运行态：
  - 双方统一：`running`

判定建议：

- 看“是否真在推流”优先使用 `active live`。
- `desired live=true` 但 `active live=false` 通常表示正在启动、网络异常或设备端尚未切换完成。

## 9.5 按需推流排查时序（实战）

标准链路：

1. 打开 Watch 后，server 收到 `/watch/start`，计算 `desired` 为 true。
2. server 通过 MQTT 发布 live:on。
3. pusher 切换为推流中，回传 `active` 为 true。
4. 关闭 Watch（或会话超时）后，server 等待 grace，再发布 live:off。
5. pusher 回传 `active` 为 false，SRS live 流消失。

建议观察点：

- server 日志：`liveDemandService` 目标态变化、`mqttControlService` publish 成功/失败。
- pusher 日志：收到 live 命令、`setLivePushEnabled` 生效、RTMP 连接状态变化。
- SRS API：`/api/v1/streams` 中该 `stream_key` 是否按预期出现/消失。

## 9.6 历史回看排查时序（实战）

标准链路：

1. Watch 首先请求 `/history/timeline`，server 默认优先用本地 `historyService`。
2. 用户 seek 时请求 `/history/play?ts=...`，返回 `source/mode/playbackUrl/offsetSec`。
3. 若本地不可播，且 edge replay 已配置，server 可回退 `source=edge`。
4. 点 “Back To Live” 时可调用 `/history/replay/stop`（edge 场景）并切回 `/live/<stream_key>.flv`。

建议观察点：

- 响应字段是否符合预期：`source`、`mode`、`playbackUrl`、`offsetSec`。
- local 回放场景下 `playbackUrl` 是否指向 `/history-files/{idx}/...` 可访问路径。
- 出现黑屏 seek 点时，先确认目标时间是否落在 `playable=true` 的 segment。

## 10. 常用接口速查

登录后（Bearer Token）常用接口：

- `GET /api/cameras`
- `GET /api/cameras/:id/stream`
- `POST /api/cameras/:id/watch/start`
- `POST /api/cameras/:id/watch/heartbeat`
- `POST /api/cameras/:id/watch/stop`
- `GET /api/cameras/:id/history/overview`
- `GET /api/cameras/:id/history/timeline?start=...&end=...`
- `GET /api/cameras/:id/history/play?ts=...&mode=...`
- `POST /api/cameras/:id/history/replay/stop`

## 11. 故障排查（按现网问题整理）

## 11.1 Dashboard 看不到 camera 在线

检查：

1. `mosquitto` 是否运行。
2. server `mqttControl.enabled` 是否为 `true`。
3. pusher `mqtt_enable` 是否为 `true` 且 topicPrefix 一致。
4. server 是否收到 state topic。

## 11.2 Watch 只有实时画面，没有时间轴缩略图/事件

检查：

1. pusher 是否启用本地录制 `enable_record`。
2. 录制目录是否存在 `segment_*.mp4` 与 `.jpg`。
3. `events.ndjson` 是否有内容。
4. server 的 `history-files` 静态目录是否可访问。

## 11.3 Seek 后无画面或部分点位黑屏

检查：

1. 目标时间是否落在有效 segment 范围。
2. 对应 segment 是否完整（非异常中断）。
3. 该 segment 是否可由 ffmpeg 正常读取（`ffprobe` 检查）。

## 11.4 延时随时间增大

优先检查：

1. SRS 是否使用当前低延迟配置（`gop_cache off`, `min_latency on`）。
2. 客户端播放缓冲是否持续增长。
3. 设备端编码与检测是否互相阻塞（当前应为检测线程独立）。

## 11.5 检测框不稳/漏检

检查：

1. `detect_interval_frames`, `detect_infer_interval_ms` 是否过大。
2. `detect_person_score_threshold` 是否过高。
3. 模型路径是否正确，TFLite 是否实际启用。

## 12. Android 客户端说明

Android 工程目录：`android/`

- 播放器分层：`Kotlin Player interface -> JNI -> C++ IPlayer/PlayerController -> FfmpegPlayer`
- 渲染链路：FFmpeg 解码 + GLES 渲染到 `SurfaceView`

FFmpeg Android so 构建：

```bash
./android/scripts/build_ffmpeg_android.sh
```

然后使用 Android Studio 打开 `android/` 目录同步构建。

## 13. 停止与清理

停止顺序建议：

1. 停止 pusher
2. 停止 server
3. 停止 SRS
4. 如需要再停止 mosquitto

命令示例：

```bash
# 前台进程直接 Ctrl+C

docker stop srs
sudo systemctl stop mosquitto
```
