# RealLive 编译、部署与运行手册

## 1. 系统架构概览

```
+------------------+       RTMP        +------------------+    HTTP-FLV     +------------------+
|   Pusher (C++)   | ----------------> |   SRS            | -------------> |   Puller (C++)   |
|   Raspberry Pi 5 |   推流 (1935)     |   媒体服务器      |   拉流 (80)    |   本地存储 MP4   |
|   CSI Camera     |                   +------------------+                +------------------+
+------------------+                          |
                                              | HTTP-FLV
                                              v
                               +-----------------------------+
                               |   Server (Node.js:3000)     |
                               |   + Vue 3 Web UI            |
                               |   用户管理 / 摄像头管理      |
                               |   前端播放 HTTP-FLV 流       |
                               +-----------------------------+
```

**数据流向：**
- Pusher 通过 RTMP 协议将 H.264 视频推送到 SRS 媒体服务器
- Puller 通过 HTTP-FLV 从 SRS 拉取视频流，存储为 MP4 分段文件
- Web UI 通过 HTTP-FLV 在浏览器中观看实时视频
- Server 提供用户认证、摄像头管理 REST API 和 WebSocket 信令

## 2. 环境要求

### 2.1 硬件

| 组件 | 硬件要求 |
|------|---------|
| Pusher | Raspberry Pi 5 (8GB RAM) + CSI 摄像头 (IMX219/IMX477) |
| Puller | Raspberry Pi 5 (或任何 Linux aarch64 主机) |
| Server + SRS | 任意 Linux 主机 (可与 Puller 同一台机器) |

### 2.2 软件依赖

**所有机器通用：**
```bash
sudo apt update && sudo apt upgrade -y
```

**Server + SRS 节点：**
```bash
# Node.js 18 LTS 或 20 LTS
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo bash -
sudo apt install -y nodejs

# SRS 依赖（从源码编译时需要）
sudo apt install -y git build-essential autoconf automake libtool pkg-config unzip
```

**Pusher 节点 (Raspberry Pi 5)：**
```bash
sudo apt install -y \
    build-essential cmake pkg-config \
    libcamera-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libasound2-dev
```

**Puller 节点 (Raspberry Pi 5)：**
```bash
sudo apt install -y \
    build-essential cmake pkg-config \
    libavformat-dev libavcodec-dev libavutil-dev
```

**C++ 单元测试（可选）：**
```bash
sudo apt install -y libgtest-dev nlohmann-json3-dev
```

## 3. 编译

### 3.1 SRS 媒体服务器

```bash
cd /opt
sudo git clone -b 5.0release https://github.com/ossrs/srs.git
cd srs/trunk
sudo ./configure
sudo make -j$(nproc)
```

编译成功后可执行文件位于 `/opt/srs/trunk/objs/srs`。

> **提示：** 也可以使用 Docker 部署 SRS，参见 [4.1 节](#41-srs-媒体服务器配置)。

### 3.2 Server (Node.js)

```bash
cd reallive/server
npm install

# 构建前端
cd web
npm install
npm run build
cd ..
```

构建完成后，前端静态文件输出到 `server/web/dist/`，Server 启动时会自动提供静态文件服务。

### 3.3 Pusher (C++)

```bash
cd reallive/pusher
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

编译成功后生成 `build/reallive-pusher`。

> **注意：** Pusher 只能在 Raspberry Pi 5 (aarch64) 上编译，因为依赖 libcamera 和 V4L2 M2M 硬件编码器。

### 3.4 Puller (C++)

```bash
cd reallive/puller
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

编译成功后生成 `build/reallive-puller`。

**编译选项：**

| CMake 选项 | 默认值 | 说明 |
|-----------|-------|------|
| `-DPLATFORM_RPI5=ON/OFF` | 自动检测 | 启用 Raspberry Pi 5 平台支持 |
| `-DENABLE_HW_DECODE=ON/OFF` | 跟随 PLATFORM_RPI5 | 启用 V4L2 M2M 硬件解码 |

### 3.5 运行单元测试（可选）

**Pusher 测试：**
```bash
cd reallive/pusher/tests
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./pusher_tests
```

**Puller 测试：**
```bash
cd reallive/puller/tests
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./puller_tests
```

**Server 测试：**
```bash
cd reallive/server
npm test
```

## 4. 部署配置

### 4.1 SRS 媒体服务器配置

#### 方式一：源码部署

创建 SRS 配置文件 `/opt/srs/trunk/conf/reallive.conf`：

```nginx
listen              1935;
max_connections     1000;
daemon              off;

http_server {
    enabled         on;
    listen          8080;
    dir             ./objs/nginx/html;
    crossdomain     on;
}

http_api {
    enabled         on;
    listen          1985;
    crossdomain     on;
}

vhost __defaultVhost__ {
    # HTTP-FLV 拉流
    http_remux {
        enabled     on;
        mount       [vhost]/[app]/[stream].flv;
    }

    # GOP 缓存，加速首帧显示
    play {
        gop_cache       on;
        gop_cache_max_frames 2500;
    }

    # 可选：HLS 支持（用于移动端兼容）
    hls {
        enabled     off;
    }
}
```

#### 方式二：Docker 部署（推荐用于快速上手）

```bash
docker run -d \
    --name srs \
    --restart always \
    -p 1935:1935 \
    -p 80:80 \
    -p 1985:1985 \
    ossrs/srs:5
```

Docker 方式使用 SRS 默认配置，已内置 RTMP 接收和 HTTP-FLV 输出，开箱即用。

#### SRS 端口说明

| 端口 | 协议 | 用途 |
|------|------|------|
| 1935 | RTMP | 接收 Pusher 推流 |
| 8080 | HTTP | HTTP-FLV 拉流 / SRS 控制台 |
| 1985 | HTTP | SRS API 接口（查看流状态等） |

#### HTTP-FLV 拉流 URL 格式

```
http://<srs-ip>/live/<stream_key>.flv
```

例如 Pusher 推流到 `rtmp://<srs-ip>:1935/live/camera01`，则拉流地址为：
```
http://<srs-ip>/live/camera01.flv
```

#### 验证 SRS 状态

```bash
# 查看 SRS API，确认服务运行
curl http://localhost:1985/api/v1/versions

# 查看当前活跃的流
curl http://localhost:1985/api/v1/streams

# 访问 SRS 控制台（浏览器）
# http://<srs-ip>
```

### 4.2 Server 配置

Server 通过环境变量配置：

| 环境变量 | 默认值 | 说明 |
|---------|-------|------|
| `PORT` | `3000` | HTTP 服务端口 |
| `JWT_SECRET` | `reallive-dev-secret-change-in-production` | JWT 签名密钥（**生产环境必须修改**） |
| `DB_PATH` | `server/data/reallive.db` | SQLite 数据库路径 |

### 4.3 Pusher 配置

编辑 `pusher/config/pusher.json`：

```json
{
    "url": "rtmp://192.168.1.100:1935/live",
    "stream_key": "camera01",
    "width": 1920,
    "height": 1080,
    "fps": 30,
    "codec": "h264",
    "bitrate": 4000000,
    "profile": "main",
    "enable_audio": false,
    "sample_rate": 44100,
    "channels": 1,
    "audio_device": "default"
}
```

| 字段 | 说明 |
|------|------|
| `url` | RTMP 服务器地址（SRS 所在 IP:1935） |
| `stream_key` | 流密钥，与 Puller 拉流 URL 中的 stream 名对应 |
| `width` / `height` | 视频分辨率 |
| `fps` | 帧率 |
| `bitrate` | 编码码率（bps） |
| `profile` | H.264 编码配置 (baseline / main / high) |
| `enable_audio` | 是否启用音频采集 |

也可通过命令行参数覆盖配置文件：

```bash
./reallive-pusher --help

Usage: reallive-pusher [options]
  -c, --config <file>   Config file path (JSON)
  -u, --url <url>       RTMP server URL
  -k, --key <key>       Stream key
  -w, --width <px>      Video width (default: 1920)
  -h, --height <px>     Video height (default: 1080)
  -f, --fps <fps>       Frame rate (default: 30)
  -b, --bitrate <bps>   Encoder bitrate (default: 4000000)
  --audio               Enable audio capture
  --help                Show this help
```

### 4.4 Puller 配置

编辑 `puller/config/puller.json`：

```json
{
    "server": {
        "url": "http://192.168.1.100/live/camera01.flv",
        "stream_key": ""
    },
    "storage": {
        "output_dir": "./recordings",
        "format": "mp4",
        "segment_duration": 3600
    },
    "decode": {
        "hardware": true
    }
}
```

| 字段 | 说明 |
|------|------|
| `url` | HTTP-FLV 拉流地址，格式 `http://<srs-ip>/live/<stream_key>.flv` |
| `output_dir` | 录像文件存储目录 |
| `format` | 输出容器格式 (mp4) |
| `segment_duration` | 录像分段时长（秒），默认 3600 = 1 小时 |
| `hardware` | 是否启用 V4L2 硬件解码 |

命令行参数：

```bash
./reallive-puller --help

Usage: reallive-puller [options]
  -c, --config <path>   Config file path (default: config/puller.json)
  -u, --url <url>       Override stream URL
  -o, --output <dir>    Override output directory
  -h, --help            Show this help message
```

## 5. 启动运行

按以下顺序启动各组件：

### 第一步：启动 SRS 媒体服务器

**源码方式：**
```bash
cd /opt/srs/trunk
./objs/srs -c conf/reallive.conf
```

**Docker 方式：**
```bash
docker start srs
```

**验证 SRS 已就绪：**
```bash
# 检查端口监听
ss -tlnp | grep -E '1935|8080|1985'

# 检查 API 响应
curl -s http://localhost:1985/api/v1/versions | head
```

### 第二步：启动 Server

```bash
cd reallive/server

# 开发模式（自动重载）
npm run dev

# 或 生产模式
JWT_SECRET="your-strong-secret-key" npm start
```

Server 启动后：
- API 服务: `http://<server-ip>:3000/api/`
- Web UI: `http://<server-ip>:3000/`
- WebSocket 信令: `ws://<server-ip>:3000/ws/signaling`

### 第三步：启动 Pusher（推流端）

```bash
cd reallive/pusher

# 使用配置文件
./build/reallive-pusher -c config/pusher.json

# 或直接指定参数
./build/reallive-pusher -u rtmp://192.168.1.100:1935/live -k camera01
```

推流成功后验证：
```bash
# 通过 SRS API 查看活跃的流
curl -s http://<srs-ip>:1985/api/v1/streams | python3 -m json.tool

# 用 ffplay 测试拉流（需要有显示器的机器上执行）
ffplay http://<srs-ip>/live/camera01.flv
```

### 第四步：启动 Puller（拉流录像端）

```bash
cd reallive/puller

# 使用配置文件
./build/reallive-puller -c config/puller.json

# 或直接指定参数
./build/reallive-puller -u http://192.168.1.100/live/camera01.flv -o ./recordings
```

Puller 会持续拉流并存储为 MP4 文件，按 `segment_duration` 自动分段。

录像文件存储在配置的 `output_dir` 目录下，命名格式：
```
recordings/
├── 20260211_143000_0000.mp4    # 第一个分段
├── 20260211_153000_0001.mp4    # 第二个分段（1小时后自动轮转）
└── ...
```

### 第五步：Web 浏览器观看

1. 打开浏览器访问 `http://<server-ip>:3000/`
2. 注册账号并登录
3. 在 Dashboard 中添加摄像头
4. 点击 "Watch Live" 观看实时画面

## 6. 停止服务

各组件均支持 `Ctrl+C` (SIGINT) 优雅停止：

```bash
# Pusher / Puller: 按 Ctrl+C，等待管线清理完成
# Server: 按 Ctrl+C

# SRS（源码方式）:
# 按 Ctrl+C（前台运行时），或：
kill $(cat /opt/srs/trunk/objs/srs.pid)

# SRS（Docker 方式）:
docker stop srs
```

## 7. 典型部署拓扑

### 单机部署（测试用）

所有组件运行在同一台 Raspberry Pi 5 上：

```
Pusher (RTMP) --> localhost:1935 (SRS) --> localhost:80 (HTTP-FLV)
                                                 |
                                           Puller 拉流录像
                                           Server:3000 Web UI
```

配置中所有 IP 使用 `localhost` 或 `127.0.0.1`。

### 多机部署（生产环境）

```
[RPi5-A: 推流端]               [服务器: SRS + Server]              [RPi5-B: 录像端]
  Pusher                          SRS (1935/8080/1985)               Puller
  CSI Camera                      Server (3000)                      MP4 Storage
                                  SQLite DB
       ---- RTMP (1935) ---->          ---- HTTP-FLV (8080) ---->
```

- 推流端 RPi5 部署在摄像头现场
- 服务器可以是任意 Linux 主机，运行 SRS + Node.js Server
- 录像端 RPi5 可部署在需要本地存储的位置
- 用户通过浏览器访问 Server 的 Web UI 远程观看

## 8. 目录结构总览

```
reallive/
├── docs/
│   ├── architecture.md      # 架构设计文档
│   ├── test-plan.md         # 测试计划
│   └── manual.md            # 本手册
├── server/                  # Node.js 后端 + Vue 前端
│   ├── src/
│   │   ├── server.js        # 入口
│   │   ├── app.js           # Express 应用
│   │   ├── config.js        # 配置
│   │   ├── routes/          # API 路由 (auth, cameras)
│   │   ├── middleware/       # JWT 鉴权中间件
│   │   ├── models/          # 数据模型 (user, camera, db)
│   │   └── signaling/       # WebSocket 信令
│   ├── web/                 # Vue 3 前端
│   │   ├── src/views/       # 页面 (Login, Register, Dashboard, Watch, MultiView)
│   │   ├── src/components/  # 组件 (Navbar, CameraCard)
│   │   ├── src/stores/      # Pinia 状态管理
│   │   └── src/api/         # API 和信令客户端
│   ├── test/                # 服务端测试
│   └── package.json
├── pusher/                  # C++ 推流端
│   ├── include/             # 接口定义
│   │   ├── platform/        # 平台抽象接口
│   │   └── core/            # 核心 Config/Pipeline
│   ├── src/
│   │   ├── main.cpp
│   │   ├── core/            # Config.cpp, Pipeline.cpp
│   │   └── platform/rpi5/   # RPi5 实现 (Libcamera, V4L2Encoder, Alsa, RTMP)
│   ├── config/pusher.json
│   ├── tests/               # 单元测试
│   └── CMakeLists.txt
├── puller/                  # C++ 拉流录像端
│   ├── include/             # 接口定义
│   │   ├── platform/        # 平台抽象接口
│   │   └── core/            # Config/PullPipeline
│   ├── src/
│   │   ├── main.cpp
│   │   ├── core/            # Config.cpp, PullPipeline.cpp
│   │   └── platform/rpi5/   # RPi5 实现 (FFmpegReceiver, V4L2Decoder, Mp4Storage)
│   ├── config/puller.json
│   ├── tests/               # 单元测试
│   └── CMakeLists.txt
└── README.md
```

## 9. 常见问题

**Q: Pusher 报 "No cameras found"**
A: 确认 CSI 摄像头连接正确，运行 `libcamera-hello` 测试摄像头是否可用。

**Q: Pusher 报 "Failed to open /dev/video11"**
A: V4L2 M2M 编码器设备号可能不同，运行 `v4l2-ctl --list-devices` 查看实际设备路径。

**Q: Pusher 推流成功但 SRS API 没有显示流**
A: 检查推流 URL 格式是否正确：`rtmp://<srs-ip>:1935/live/<stream_key>`。通过 `curl http://<srs-ip>:1985/api/v1/streams` 确认。

**Q: Puller 连接 HTTP-FLV 超时**
A: 确认 SRS 已启动且有活跃推流。先用 `curl -v http://<srs-ip>/live/<key>.flv` 测试是否能收到数据。

**Q: Puller 报 "Failed to find stream info"**
A: HTTP-FLV 流必须有 Pusher 正在推流才能拉取。确认 Pusher 已启动并成功推流后再启动 Puller。

**Q: Web UI 登录后页面空白**
A: 确认已执行 `cd server/web && npm run build`，前端静态文件需要构建后才能被 Server 提供。

**Q: 录像文件无法播放**
A: 确保 Puller 是通过 `Ctrl+C` 正常停止的，非正常退出可能导致 MP4 文件未正确写入 trailer。可用 `ffprobe <file>.mp4` 检查文件完整性。

**Q: SRS 在 ARM (Raspberry Pi) 上编译失败**
A: SRS 5.0 支持 aarch64 架构。确保使用 `5.0release` 分支。也可直接使用 Docker 方式部署：`docker run -d -p 1935:1935 -p 80:80 -p 1985:1985 ossrs/srs:5`。
