# RealLive

Real-time video surveillance system with live streaming support.

## Components

- **Server** - Node.js backend with Express.js, SQLite, and Vue 3 frontend
- **Pusher** - C++ cross-platform stream publisher (camera capture + encoding + streaming)
- **Puller** - C++ cross-platform stream receiver (receiving + decoding + local storage)

## Architecture

The system uses WebRTC for low-latency real-time video streaming with WebSocket-based signaling. See [docs/architecture.md](docs/architecture.md) for the full architecture design.

## Current Platform

- Server: Any platform with Node.js
- Pusher: Raspberry Pi 5 + CSI Camera (libcamera + V4L2 M2M H.264)
- Puller: Raspberry Pi 5 (FFmpeg + V4L2 hardware decode + MP4 storage)

## Quick Start

### Server

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
