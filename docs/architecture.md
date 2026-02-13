# RealLive System Architecture

## 1. System Overview

RealLive is a video surveillance system consisting of four major components:

```
+------------------+          +------------------+          +------------------+
|   Pusher (C++)   |   RTMP   |   SRS Media      | HTTP-FLV |   Web UI (Vue)   |
|   Raspberry Pi 5 | -------> |   Server         | -------> |   Browser        |
|   CSI Camera     |          |                  |          |   mpegts.js      |
+------------------+          +------------------+          +------------------+
                                      |
                                      | HTTP-FLV
                                      v
                              +------------------+
                              |   Puller (C++)   |
                              |   Raspberry Pi 5 |
                              |   Local Storage  |
                              +------------------+

+------------------+
|   Server         |  HTTP REST API + WebSocket (camera status)
|   Node.js        |  User auth, camera management, status notifications
+------------------+
```

### Module Responsibilities

| Module    | Technology       | Role                                        |
|-----------|-----------------|---------------------------------------------|
| Server    | Node.js/Express | Authentication, API, camera status signaling |
| Web UI    | Vue 3           | Camera management, live HTTP-FLV playback   |
| Pusher    | C++             | Camera capture, encoding, RTMP push to SRS  |
| Puller    | C++             | HTTP-FLV receive from SRS, decode, storage  |
| SRS       | Media Server    | RTMP ingest, HTTP-FLV output                |

## 2. Communication Protocols

### Media: RTMP (Push)
- Pusher publishes live stream to SRS via RTMP
- H.264 video + AAC audio
- Reliable TCP-based delivery

### Media: HTTP-FLV (Pull)
- Web UI and Puller receive live stream from SRS via HTTP-FLV
- Low latency (1-3s typical)
- Native browser support via mpegts.js
- URL format: `http://<srs-host>/live/<stream_key>.flv`

### Signaling: WebSocket
- Path: `/ws/signaling`
- Camera status updates (online/offline/streaming)
- Real-time notifications
- JSON message format

### Business API: HTTP REST
- User authentication (register, login)
- Camera CRUD operations
- Session management
- JSON request/response format

## 3. Server Architecture

### Technology Stack
- **Runtime**: Node.js
- **Framework**: Express.js
- **WebSocket**: Socket.io (camera status signaling)
- **Database**: SQLite3 (via better-sqlite3)
- **Authentication**: JWT (jsonwebtoken + bcrypt)

### Server Modules
```
server/
├── src/
│   ├── app.js              # Express app setup
│   ├── server.js           # Entry point
│   ├── config.js           # Configuration
│   ├── routes/
│   │   ├── auth.js         # Authentication routes
│   │   └── cameras.js      # Camera CRUD routes
│   ├── middleware/
│   │   └── auth.js         # JWT middleware
│   ├── models/
│   │   ├── db.js           # SQLite setup
│   │   ├── user.js         # User model
│   │   └── camera.js       # Camera model
│   └── signaling/
│       └── index.js        # Camera status signaling via Socket.io
├── web/                    # Vue frontend
└── package.json
```

## 4. Database Design

### SQLite Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE cameras (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    stream_key TEXT UNIQUE NOT NULL,
    status TEXT DEFAULT 'offline',    -- offline, online, streaming
    resolution TEXT DEFAULT '1080p',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id INTEGER NOT NULL,
    start_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    end_time DATETIME,
    status TEXT DEFAULT 'active',     -- active, ended, error
    FOREIGN KEY (camera_id) REFERENCES cameras(id) ON DELETE CASCADE
);
```

### Entity Relationships
```
users 1---* cameras 1---* sessions
```

## 5. Media Server (SRS)

### Role
SRS (Simple Realtime Server) serves as the media relay:
- Receives RTMP streams from Pusher
- Outputs HTTP-FLV streams to Web UI and Puller

### Stream URL Mapping
- RTMP push: `rtmp://<srs-host>/live/<stream_key>`
- HTTP-FLV pull: `http://<srs-host>/live/<stream_key>.flv`

### Key Configuration
```conf
listen              1935;
max_connections     1000;
http_server {
    enabled         on;
    listen          80;
}
vhost __defaultVhost__ {
    http_remux {
        enabled     on;
    }
}
```

## 6. Pusher Architecture (C++)

### Platform Abstraction Layer

```cpp
// ICameraCapture - Camera capture abstraction
class ICameraCapture {
public:
    virtual ~ICameraCapture() = default;
    virtual bool open(const CaptureConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual Frame captureFrame() = 0;
};

// IAudioCapture - Audio capture abstraction
class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool open(const AudioConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual AudioFrame captureFrame() = 0;
};

// IEncoder - Encoder abstraction
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool init(const EncoderConfig& config) = 0;
    virtual EncodedPacket encode(const Frame& frame) = 0;
    virtual void flush() = 0;
};

// IStreamer - RTMP streaming abstraction
class IStreamer {
public:
    virtual ~IStreamer() = default;
    virtual bool connect(const std::string& url, const std::string& streamKey) = 0;
    virtual bool sendPacket(const EncodedPacket& packet) = 0;
    virtual void disconnect() = 0;
};
```

### Raspberry Pi 5 Implementation
- **Camera Capture**: libcamera API for CSI camera access
- **Video Encoding**: V4L2 M2M hardware H.264 encoder (Broadcom VideoCore)
- **Audio Capture**: ALSA (Advanced Linux Sound Architecture)
- **Streaming**: FFmpeg libavformat for RTMP output to SRS

### Pusher Pipeline
```
CSI Camera --> libcamera capture --> V4L2 M2M H.264 encode --> RTMP push to SRS
Microphone --> ALSA capture -----> AAC encode --------------> RTMP push to SRS
```

## 7. Puller Architecture (C++)

### Platform Abstraction Layer

```cpp
// IStreamReceiver - Stream receiving abstraction
class IStreamReceiver {
public:
    virtual ~IStreamReceiver() = default;
    virtual bool connect(const std::string& url) = 0;
    virtual EncodedPacket receivePacket() = 0;
    virtual void disconnect() = 0;
};

// IDecoder - Decoder abstraction
class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual bool init(const DecoderConfig& config) = 0;
    virtual Frame decode(const EncodedPacket& packet) = 0;
    virtual void flush() = 0;
};

// IStorage - Storage abstraction
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool open(const std::string& path, const StorageConfig& config) = 0;
    virtual bool writePacket(const EncodedPacket& packet) = 0;
    virtual void close() = 0;
};

// IRenderer - Renderer abstraction (optional, for preview)
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(const RenderConfig& config) = 0;
    virtual void renderFrame(const Frame& frame) = 0;
    virtual void close() = 0;
};
```

### Raspberry Pi 5 Implementation
- **Stream Receiving**: FFmpeg libavformat for HTTP-FLV input from SRS
- **Decoding**: V4L2 M2M hardware H.264 decoder
- **Storage**: FFmpeg muxer for MP4 file output
- **Rendering**: DRM/KMS for optional local preview

### Puller Pipeline
```
HTTP-FLV from SRS --> FFmpeg demux --> V4L2 M2M H.264 decode --> MP4 muxer --> Local file
                                                              --> DRM/KMS   --> Display (optional)
```

## 8. API Interface Design

### Authentication

| Method | Path                | Description          | Auth |
|--------|---------------------|----------------------|------|
| POST   | /api/auth/register  | Register new user    | No   |
| POST   | /api/auth/login     | Login, returns JWT   | No   |

### Camera Management

| Method | Path                     | Description              | Auth |
|--------|--------------------------|--------------------------|------|
| GET    | /api/cameras             | List user's cameras      | Yes  |
| POST   | /api/cameras             | Register new camera      | Yes  |
| PUT    | /api/cameras/:id         | Update camera info       | Yes  |
| DELETE | /api/cameras/:id         | Remove camera            | Yes  |
| GET    | /api/cameras/:id/stream  | Get stream info (FLV URL)| Yes  |

### WebSocket Signaling

| Event              | Direction        | Description                    |
|--------------------|------------------|--------------------------------|
| join-room          | Client -> Server | Join camera's status room      |
| camera-status      | Server -> Client | Camera online/offline/streaming|
| stream-start       | Client -> Server | Pusher notifies stream started |
| stream-stop        | Client -> Server | Pusher notifies stream stopped |

## 9. Project Directory Structure

```
reallive/
├── docs/                    # Documentation
│   └── architecture.md      # This file
├── server/                  # Node.js server + Vue frontend
│   ├── src/                 # Backend source code
│   │   ├── app.js
│   │   ├── server.js
│   │   ├── config.js
│   │   ├── routes/
│   │   ├── middleware/
│   │   ├── models/
│   │   └── signaling/
│   ├── web/                 # Vue 3 frontend
│   └── package.json
├── pusher/                  # C++ stream pusher
│   ├── include/             # Public headers
│   │   ├── ICameraCapture.h
│   │   ├── IAudioCapture.h
│   │   ├── IEncoder.h
│   │   └── IStreamer.h
│   ├── src/                 # Core source code
│   │   └── main.cpp
│   ├── platform/            # Platform implementations
│   │   └── rpi5/            # Raspberry Pi 5
│   └── CMakeLists.txt
├── puller/                  # C++ stream puller
│   ├── include/             # Public headers
│   │   ├── IStreamReceiver.h
│   │   ├── IDecoder.h
│   │   ├── IStorage.h
│   │   └── IRenderer.h
│   ├── src/                 # Core source code
│   │   └── main.cpp
│   ├── platform/            # Platform implementations
│   │   └── rpi5/            # Raspberry Pi 5
│   └── CMakeLists.txt
└── README.md
```

## 10. Security Considerations

- All passwords hashed with bcrypt (cost factor >= 12)
- JWT tokens with expiration (24h default)
- Stream keys are unique random UUIDs
- HTTPS in production
- SRS supports stream authentication via HTTP callbacks
- Input validation on all API endpoints
