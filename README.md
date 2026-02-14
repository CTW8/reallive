# RealLive

Real-time video surveillance system with live streaming support.

## Components

- **Server** - Node.js backend with Express.js, SQLite, and Vue 3 frontend
- **Pusher** - C++ stream publisher (camera capture + H.264 encoding + RTMP push)
- **Puller** - C++ stream receiver (HTTP-FLV receive + decoding + MP4 storage)
- **SRS** - Media server for RTMP ingest and HTTP-FLV output

## Architecture

```
Pusher --RTMP--> SRS --HTTP-FLV--> Web UI (mpegts.js)
                     --HTTP-FLV--> Puller (FFmpeg -> MP4)
```

The system uses RTMP for stream publishing and HTTP-FLV for playback. SRS media server handles protocol conversion. See [docs/architecture.md](docs/architecture.md) for the full architecture design.

## Current Platform

- Server: Any platform with Node.js
- SRS: Linux (Docker or source build)
- Pusher: Raspberry Pi 5 + CSI Camera (libcamera + V4L2 M2M H.264)
- Puller: Raspberry Pi 5 (FFmpeg + V4L2 hardware decode + MP4 storage)

## Quick Start

### SRS Media Server

```bash
docker run -d --name srs \
  -p 1935:1935 -p 80:80 \
  ossrs/srs:5
```

### Server

配置文件：`server/config/server.json`

```json
{
  "port": 80,
  "srsApi": "http://localhost:1985",
  "edgeReplay": {
    "url": "http://127.0.0.1:8090",
    "timeoutMs": 1500
  }
}
```

```bash
cd server
npm install
npm run dev
```

### Pusher (Raspberry Pi 5)

```bash
cd pusher
mkdir build && cd build
cmake ..
make
./reallive-pusher
```

### Puller (Raspberry Pi 5)

```bash
cd puller
mkdir build && cd build
cmake ..
make
./reallive-puller
```

## Project Structure

```
reallive/
├── docs/           # Documentation
├── server/         # Node.js server + Vue frontend
├── pusher/         # C++ stream pusher
├── puller/         # C++ stream puller
└── README.md
```

## License

All rights reserved.
