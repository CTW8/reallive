# RealLive System Architecture

This document describes the architecture aligned with the current codebase state.

Quick snapshot: [system-understanding.md](system-understanding.md)

## 1. Scope and Design Goals

RealLive is a home surveillance platform with these goals:

- Low-latency live preview for browser and Android clients.
- Demand-driven live pushing to reduce edge resource usage.
- Local segmented recording on device with history playback by time.
- Person detection with timeline events and overlay boxes.
- Unified control and observability through REST, WebSocket, MQTT, and SEI.

## 2. High-Level Topology

```text
+------------------+       RTMP        +------------------+
| Pusher (C++)     | ----------------> | SRS              |
| - capture        |                   | - ingest RTMP    |
| - encode         |                   | - output HTTP-FLV|
| - detect + SEI   |                   +---------+--------+
| - local record   |                             |
+--------+---------+                             | HTTP-FLV
         |                                       |
         | MQTT state/command                    v
         |                               +------------------+
         |                               | Server (Node.js) |
         |                               | - API + auth     |
         |                               | - stream status  |
         |                               | - history API    |
         |                               | - live demand    |
         |                               +----+--------+----+
         |                                    |        |
         |                                    |        | Socket.io signaling
         v                                    |        |
+------------------+                          |        v
| MQTT broker      |                          |   +------------------+
+------------------+                          |   | Web UI (Vue)     |
                                              |   | mpegts.js + chart |
                                              |   +------------------+
                                              |
                                              | HTTP-FLV proxy + REST
                                              v
                                       +------------------+
                                       | Android client   |
                                       | Kotlin/JNI/C++   |
                                       +------------------+
```

Note: `puller` still exists in the repository as an auxiliary HTTP-FLV ingest/record module.
The current primary history path for watch playback is `pusher local recording + server historyService`.

## 3. Media Plane

### 3.1 Live stream path

1. `pusher` captures camera frames and encodes H.264.
2. It injects SEI user-data payload (telemetry/config/person/event).
3. It pushes RTMP to SRS: `rtmp://<srs>/live/<stream_key>`.
4. SRS remuxes to HTTP-FLV.
5. Clients play via server proxy path: `/live/<stream_key>.flv`.

### 3.2 History playback path

- Device local recorder writes segmented MP4 files and JPEG thumbnails.
- Server provides history API for timeline and point-in-time playback.
- Playback modes:
  - `local`: direct file-backed playback mapping from segment metadata.
  - `edge`: replay delegation through edge replay service if enabled.

History API behavior is local-first by default:

- `GET /api/cameras/:id/history/overview`
- `GET /api/cameras/:id/history/timeline?start=&end=`
- `GET /api/cameras/:id/history/play?ts=&mode=`

On playback selection:

- server returns `source=local` when local segments are playable.
- server falls back to `source=edge` only when local cannot satisfy playback and edge replay is enabled.
- caller can force edge with `mode=edge` (or `source=edge`) in play query.

### 3.3 History seek and back-to-live sequence

```text
Watch UI                 Server(API)            historyService            edgeReplayService            Player
   |                          |                       |                           |                      |
   | GET /history/timeline    |                       |                           |                      |
   |------------------------->| query local timeline  |                           |                      |
   |                          |---------------------->|                           |                      |
   |                          |<----------------------| segments/events/thumbs    |                      |
   |<-------------------------| source=local, timeline payload                    |                      |
   | render filmstrip + marks |                       |                           |                      |
   |                          |                       |                           |                      |
   | GET /history/play?ts=T   |                       |                           |                      |
   |------------------------->| select local playback |                           |                      |
   |                          |---------------------->|                           |                      |
   |                          |<----------------------| mode=history,url,offset   |                      |
   |<-------------------------| source=local,playbackUrl,offsetSec                |                      |
   | switch player to history |---------------------------------------------------------------> play url  |
   |                          |                       |                           |                      |
   | (optional) force edge    | start replay on edge  |                           |                      |
   | GET /history/play?mode=edge&ts=T                 |-------------------------->|                      |
   |                          |<--------------------------------------------------| source=edge,url     |
   |<-------------------------| source=edge,playbackUrl,sessionId                |                      |
   |                          |                       |                           |                      |
   | click Back To Live       | POST /history/replay/stop (best effort for edge) |                      |
   |------------------------->|----------------------------------------------->    |                      |
   | switch to /live/<key>.flv|                                                           play live url  |
```

Operational notes:

- `offsetSec` is computed from segment start and requested timestamp for local playback.
- Local open-writing segments are marked not playable; seek targets nearest playable segment.
- `history/replay/stop` mainly affects edge replay sessions; local file playback switch does not require stop RPC.

## 4. Control Plane

### 4.1 Watch-session driven live demand

Server API:

- `POST /api/cameras/:id/watch/start`
- `POST /api/cameras/:id/watch/heartbeat`
- `POST /api/cameras/:id/watch/stop`

Service behavior (`liveDemandService`):

- Maintains in-memory viewer sessions per camera.
- Marks desired live state based on active viewers.
- Applies grace period before stopping live push.
- Reconciles desired state periodically.

Default timing (overridable by env):

- heartbeat interval: 10s
- session ttl: 25s
- no-viewer stop grace: 30s
- reconcile interval: 1s

### 4.2 Runtime control transport

Primary transport is MQTT (`mqttControlService`):

- Command topic: `<prefix>/<stream_key>/command`
- State topic: `<prefix>/<stream_key>/state`

If MQTT is unavailable, control can fallback via `edgeReplayService` runtime endpoint.

Device-side runtime handler (`MqttRuntimeClient`) applies `enable/disable live push` to `Pipeline` without restarting process.

### 4.3 Unified state terminology

To avoid naming confusion across transport layers, use this mapping:

- Concept `desired live`:
  - server internal: `desiredLive`
  - MQTT/control payload: `desired_live`
- Concept `active live`:
  - server internal: `activeLive`
  - MQTT/control payload: `active_live`
- Concept `process running`:
  - server/internal and MQTT payload: `running`

Interpretation:

- `desired live` means target state requested by watch-session logic.
- `active live` means pipeline is actually sending live media.
- `running` means pusher process/pipeline is alive, not equal to streaming.

### 4.4 Watch-to-stream state sequence

```text
Watch UI          Server(liveDemand)      mqttControl        MQTT Broker         Pusher           SRS
   |                     |                     |                   |                 |               |
   | POST /watch/start   |                     |                   |                 |               |
   |-------------------->| desiredLive=true    | publish live:on   |                 |               |
   |                     |-------------------->|------------------>| command         |               |
   |                     |                     |                   |---------------> | setLivePush=1 |
   |                     |                     |                   |                 | RTMP connect   |
   |                     |                     |                   |                 |--------------> |
   |                     |                     |                   |                 | state(active)  |
   |                     |<--------------------| state topic <-----|<--------------- |               |
   | receive camera status via API/signaling   |                   |                 |               |
   |                     |                     |                   |                 |               |
   | POST /watch/heartbeat (every 10s)         |                   |                 |               |
   |-------------------->| extend session ttl  |                   |                 |               |
   |                     |                     |                   |                 |               |
   | POST /watch/stop    | noViewerSince=now   |                   |                 |               |
   |-------------------->| wait grace(30s)     |                   |                 |               |
   |                     | desiredLive=false    | publish live:off  |                 |               |
   |                     |-------------------->|------------------>| command         |               |
   |                     |                     |                   |---------------> | setLivePush=0 |
   |                     |<--------------------| state topic <-----|<--------------- | active=false  |
```

Operational notes:

- Session expiration (`session ttl`) is equivalent to an implicit watch stop.
- `desired live=true` and `active live=false` is a transitional state and should clear quickly.
- If MQTT state is stale beyond `stateStaleMs`, server falls back to SRS/DB signals for UI status.

## 5. Server Architecture (Node.js)

### 5.1 Core modules

- `src/app.js`
  - Express app setup.
  - Proxies `/live/*` and `/history/*` to SRS.
  - Serves static history file roots as `/history-files/{idx}`.
- `src/server.js`
  - Starts HTTP server and Socket.io signaling.
  - Boots `srsSync`, `mqttControlService`, `liveDemandService`.
- `src/signaling/index.js`
  - JWT-authenticated Socket.io namespace (`/ws/signaling`).
  - Dashboard snapshot and activity events.

### 5.2 Service responsibilities

- `srsSync`
  - Polls SRS stream API every second.
  - Tracks active streams and stream stats cache.
  - Updates camera status transitions with debounce.
- `seiMonitor`
  - Pulls FLV stream, parses H.264 SEI, caches telemetry/events.
- `historyService`
  - Scans recording roots, segments, thumbnails, `events.ndjson`.
  - Builds overview/timeline/playback responses.
  - Aligns relative event timestamps to segment time domain when needed.
- `liveDemandService`
  - Watch session state machine and desired-live reconciliation.
- `mqttControlService`
  - MQTT command publish and runtime state cache.
  - Emits state-change events to server signaling.
- `edgeReplayService`
  - Optional adapter for edge replay and runtime APIs.

### 5.3 API surface (current)

Auth:

- `POST /api/auth/register`
- `POST /api/auth/login`

Camera + stream:

- `GET /api/cameras`
- `POST /api/cameras`
- `PUT /api/cameras/:id`
- `DELETE /api/cameras/:id`
- `GET /api/cameras/:id/stream`

Watch demand:

- `POST /api/cameras/:id/watch/start`
- `POST /api/cameras/:id/watch/heartbeat`
- `POST /api/cameras/:id/watch/stop`

History:

- `GET /api/cameras/:id/history/overview`
- `GET /api/cameras/:id/history/timeline?start=&end=`
- `GET /api/cameras/:id/history/play?ts=`
- `POST /api/cameras/:id/history/replay/stop`

Dashboard/session:

- `GET /api/dashboard/stats`
- `GET /api/sessions`
- `GET /api/sessions/active`
- `GET /api/health`

### 5.4 State precedence (camera online/streaming)

Status shown to clients is merged from multiple sources:

1. MQTT runtime state (`desired_live/active_live/running`, normalized as `desiredLive/activeLive/running`) if present and fresh.
2. SRS sync stream activity.
3. DB camera status fallback.

This avoids stale UI state when media path and control path are temporarily inconsistent.

## 6. Pusher Architecture (C++)

### 6.1 Pipeline model

`Pipeline` is multi-threaded with decoupled detection:

- capture thread: fetch camera frames.
- detect thread: motion gate + TFLite inference.
- send thread: overlay, encode, RTMP send, record write, SEI injection.
- audio thread (optional): ALSA capture and send.

This preserves live frame throughput by preventing detector stalls from blocking encode/send.

### 6.2 Detection path

Current logic:

- Stage 1: motion detection (OpenCV when available, fallback otherwise).
- Stage 2: TFLite person inference (YOLOv8n tflite model in config).
- Output:
  - on-frame person box overlay (if enabled).
  - throttled person event journal (`events.ndjson`).
  - SEI fields `person` and `events`.

### 6.3 SEI payload

Pusher injects JSON payload into H.264 SEI user-data with stable UUID.

Main fields:

- `device`: cpu/memory/storage and per-core cpu load.
- `camera`: effective camera/encoder parameters.
- `configurable`: adjustable parameter ranges/options.
- `person`: latest tracked box state.
- `events`: recent person_detected events.

### 6.4 Local recording

`LocalRecorder`:

- writes temp-open segment files, finalizes to `segment_<start>_<end>.mp4`.
- optionally generates `.jpg` thumbnails.
- rotates by segment duration with keyframe-aware boundary.
- low-space cleanup:
  - start cleanup below `minFreePercent`.
  - delete oldest until reaching `targetFreePercent`.

### 6.5 Runtime control endpoints on device

`ControlServer` (if enabled) exposes:

- `GET /api/record/overview`
- `GET /api/record/timeline`
- `POST /api/record/replay/start`
- `POST /api/record/replay/stop`
- `GET /api/runtime/status`
- `POST /api/runtime/live`

Replay start launches ffmpeg process to republish selected segment as FLV live stream.

## 7. Web Architecture (Vue)

### 7.1 Dashboard

- Camera cards with status, heartbeat/runtime hints, and latest thumbnail.
- Activity feed receives signaling events (`stream-start`, `stream-stop`, `person-detected`).

### 7.2 Watch view

- Live FLV playback by `mpegts.js`.
- History mode with timeline slider and seek-by-timestamp playback switch.
- Filmstrip timeline with evenly distributed thumbnails.
- Event markers and event list integrated into timeline.
- Timeline zoom (slider + mouse wheel).
- "Back To Live" transition.
- SEI cards:
  - Device usage chart (CPU/MEM/DISK).
  - Per-core CPU chart.
  - Camera config card.
  - Configurable options card.

Chart stack uses ECharts loaded by dynamic import.

## 8. Android Architecture

Android client is native-player based (no WebView):

- Kotlin API layer: `Player` interface.
- Kotlin JNI implementation: `NativePlayer`.
- JNI bridge: `NativePlayerJni.cpp`.
- Native controller/interface: `PlayerController` + `IPlayer`.
- Native implementation: `FfmpegPlayer`.

Playback stack:

- FFmpeg demux (`avformat`) from HTTP-FLV.
- FFmpeg decode (`avcodec`).
- RGBA conversion (`swscale`).
- GLES render to `SurfaceView`.

## 9. Data and Storage Layout

Server database (SQLite):

- `users`
- `cameras`
- `sessions`

Device recording layout (per stream key):

- `segment_<start>_<end>.mp4`
- `segment_<start>_<end>.jpg`
- `events.ndjson`

Server history service supports multiple recording roots and exposes files under `/history-files/{idx}`.

### 9.1 Puller module (auxiliary)

`puller` remains available for dedicated ingest/storage workflows:

- receives HTTP-FLV from SRS,
- decodes and stores media locally,
- can be used for offline archival experiments.

It is not the primary source of timeline data for the current Web watch flow.

## 10. Configuration Map

Main config entry points:

- server: `server/config/server.json`
- pusher: `pusher/config/pusher.json`
- srs: `server/srs.conf`
- android native module: `android/player-native/*`

Important server runtime config groups:

- `edgeReplay`
- `mqttControl`

Important pusher runtime config groups:

- stream/camera/encoder
- record
- control
- mqtt
- detection

## 11. Reliability and Performance Notes

- SRS sync uses in-flight guard and offline grace polling to reduce flapping.
- MQTT device state has staleness eviction.
- Timeline/event merge logic deduplicates repeated events.
- Detection is decoupled from encode/send for smoother live pipeline.
- Live player path applies low-latency buffer control in Web watch page.

## 12. Known Constraints

- Main live and replay transport is HTTP-FLV (not WebRTC).
- End-to-end consistency depends on multiple signals (MQTT, SRS, DB), so short temporary mismatches can still occur.
- Edge replay behavior depends on external edge runtime availability/configuration.
- Android player currently focuses video path; advanced A/V sync and richer player state callbacks can be extended.

## 13. Related docs

- Current implementation summary: `system-understanding.md`
- Setup and operations: `manual.md`
- Validation plan: `test-plan.md`
