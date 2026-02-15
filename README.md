# RealLive

Real-time video surveillance system with live streaming support.

## Components

### Primary path

- **Pusher** - C++ edge process (camera capture + encoding + RTMP push + local recording + detection)
- **SRS** - Media server (RTMP ingest + HTTP-FLV output)
- **Server** - Node.js backend (API/signaling/history aggregation/live-demand control) + Vue Web UI
- **Android** - Native client (Kotlin + JNI + C++ HTTP-FLV player)

### Optional/Auxiliary

- **Puller** - C++ HTTP-FLV ingest/storage module for auxiliary recording experiments

## Architecture

```
Pusher --RTMP--> SRS --HTTP-FLV--> Web UI (mpegts.js)
                     --HTTP-FLV--> Android (native player)
                     --HTTP-FLV--> Puller (optional, FFmpeg -> MP4)
```

The system uses RTMP for stream publishing and HTTP-FLV for playback. SRS media server handles protocol conversion. See [docs/architecture.md](docs/architecture.md) for the full architecture design.

Runtime state naming note:

- Server internal/API often uses `desiredLive` / `activeLive`
- MQTT/control payload uses `desired_live` / `active_live`
- They represent the same semantics (`desired` target vs `active` actual)

## Documentation

- Architecture (code-aligned): [docs/architecture.md](docs/architecture.md)
- System understanding snapshot: [docs/system-understanding.md](docs/system-understanding.md)
- Build/deploy/run manual: [docs/manual.md](docs/manual.md)
- Test plan: [docs/test-plan.md](docs/test-plan.md)

## Current Platform

- Server: Any platform with Node.js
- SRS: Linux (Docker or source build)
- Pusher: Raspberry Pi 5 + CSI Camera (libcamera + FFmpeg encoder + MQTT runtime control)
- Android: Android 8+ (SDK 34/NDK r26+ for native module build)
- Puller: Raspberry Pi 5 (optional auxiliary module for HTTP-FLV ingest/storage experiments)

## Quick Start

### 1. MQTT Broker

```bash
sudo apt install -y mosquitto
sudo systemctl enable --now mosquitto
```

### 2. SRS Media Server

Use repository SRS config (`server/srs.conf`):
Run this from repository root.

```bash
docker run -d --name srs --restart unless-stopped \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 \
  -v "$(pwd)/server/srs.conf:/usr/local/srs/conf/srs.conf" \
  ossrs/srs:5 ./objs/srs -c conf/srs.conf
```

### 3. Server

配置文件：`server/config/server.json`

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

```bash
cd server
npm install
npm run dev
```

If `port` is `80`, Linux may require elevated privilege or capability.

### 4. Pusher (Raspberry Pi 5)

```bash
cd pusher
mkdir build && cd build
cmake ..
make
./reallive-pusher -c ../config/pusher.json
```

### 5. Puller (optional)

```bash
cd puller
mkdir build && cd build
cmake ..
make
./reallive-puller -c ../config/puller.json
```

### 6. Verify

```bash
# SRS API
curl -s http://127.0.0.1:1985/api/v1/versions
curl -s http://127.0.0.1:1985/api/v1/streams

# Server health
curl -s http://127.0.0.1:80/api/health
```

Web UI:

- Open `http://127.0.0.1:80/`

Android:

- Open `android/` in Android Studio
- See `android/README.md` for native player build details

## Project Structure

```
reallive/
├── docs/           # Documentation
├── android/        # Android app + native player module
├── server/         # Node.js server + Vue frontend
├── pusher/         # C++ stream pusher
├── puller/         # C++ stream puller
└── README.md
```

## License

All rights reserved.
