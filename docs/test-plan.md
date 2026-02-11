# RealLive Test Plan

## 1. Test Scope and Objectives

### Scope
- **Server**: Node.js/Express REST API, JWT authentication, SQLite database, Socket.io signaling
- **Pusher**: C++ camera capture, encoding, WebRTC streaming on Raspberry Pi 5
- **Puller**: C++ stream receiving, decoding, MP4 storage on Raspberry Pi 5
- **Web UI**: Vue 3 frontend camera management and live stream playback
- **End-to-End**: Full pipeline from camera capture to playback and recording

### Objectives
1. Validate all REST API endpoints return correct responses and status codes
2. Verify user authentication flow (register, login, JWT token lifecycle)
3. Confirm Camera CRUD operations and ownership enforcement
4. Test WebSocket signaling message routing between pusher, puller, and Web UI
5. Verify C++ interface implementations compile and link correctly
6. Validate pusher configuration file parsing and error handling
7. Validate puller storage and file rotation logic
8. Measure end-to-end latency, bandwidth usage, and resource consumption
9. Confirm the full pipeline works: capture -> encode -> stream -> receive -> store

## 2. Test Environment

### Hardware
- **Target Platform**: Raspberry Pi 5 (8GB RAM, Broadcom BCM2712)
- **Camera**: CSI camera module (IMX219/IMX477)
- **Storage**: microSD / USB SSD for recording output
- **Network**: Local Ethernet or WiFi (for latency measurement)

### Software
- **OS**: Raspberry Pi OS (Debian 12 Bookworm, 64-bit)
- **Node.js**: v18 LTS or v20 LTS
- **C++ Compiler**: GCC 12+ with C++17 support
- **CMake**: 3.22+
- **FFmpeg**: 5.x+ (for puller MP4 muxing)
- **libcamera**: System-provided (for pusher capture)
- **SQLite**: 3.40+

### Dependencies
- `curl` for API test scripts
- `jq` for JSON parsing in shell tests
- Google Test (gtest) for C++ unit tests
- `websocat` or `wscat` for WebSocket testing (optional)

## 3. Unit Test Plan

### 3.1 Server Unit Tests

| Test ID | Component        | Test Description                              | Expected Result                      |
|---------|------------------|-----------------------------------------------|--------------------------------------|
| S-001   | Auth Register    | Register with valid username/email/password   | 201, user object returned            |
| S-002   | Auth Register    | Register with duplicate username              | 409 Conflict                         |
| S-003   | Auth Register    | Register with missing fields                  | 400 Bad Request                      |
| S-004   | Auth Login       | Login with valid credentials                  | 200, JWT token returned              |
| S-005   | Auth Login       | Login with wrong password                     | 401 Unauthorized                     |
| S-006   | Auth Login       | Login with non-existent user                  | 401 Unauthorized                     |
| S-007   | Camera Create    | Create camera with valid auth token           | 201, camera object with stream_key   |
| S-008   | Camera List      | List cameras belonging to authenticated user  | 200, array of cameras                |
| S-009   | Camera Update    | Update camera name with valid auth            | 200, updated camera object           |
| S-010   | Camera Delete    | Delete own camera                             | 200, success message                 |
| S-011   | Camera Delete    | Delete another user's camera                  | 403 Forbidden                        |
| S-012   | Camera Stream    | Get stream info for own camera                | 200, stream connection details       |
| S-013   | Auth Middleware   | Access protected route without token          | 401 Unauthorized                     |
| S-014   | Auth Middleware   | Access protected route with expired token     | 401 Unauthorized                     |
| S-015   | WebSocket        | Connect to signaling namespace                | Connection established               |
| S-016   | WebSocket        | Join camera room                              | Room joined confirmation             |
| S-017   | WebSocket        | Send WebRTC offer, receive answer relay       | Offer forwarded to room peers        |

### 3.2 Pusher Unit Tests

| Test ID | Component          | Test Description                        | Expected Result                    |
|---------|--------------------|-----------------------------------------|------------------------------------|
| P-001   | Config Parser      | Parse valid JSON config file            | All fields populated correctly     |
| P-002   | Config Parser      | Parse config with missing fields        | Default values used or error       |
| P-003   | Config Parser      | Parse invalid JSON                      | Error reported gracefully          |
| P-004   | ICameraCapture     | Mock camera open/start/stop lifecycle   | State transitions are correct      |
| P-005   | IEncoder           | Mock encoder init/encode/flush          | Encoded packets returned           |
| P-006   | IStreamer          | Mock streamer connect/send/disconnect   | Connection state managed correctly |

### 3.3 Puller Unit Tests

| Test ID | Component          | Test Description                        | Expected Result                    |
|---------|--------------------|-----------------------------------------|------------------------------------|
| R-001   | Config Parser      | Parse valid JSON config file            | All fields populated correctly     |
| R-002   | IStreamReceiver    | Mock receiver connect/receive/disconnect| State transitions are correct      |
| R-003   | IDecoder           | Mock decoder init/decode/flush          | Decoded frames returned            |
| R-004   | IStorage           | Mock storage open/write/close           | Write operations tracked           |
| R-005   | IStorage           | Storage file rotation on size limit     | New file created at threshold      |
| R-006   | IStorage           | Storage disk space check                | Error when disk full               |

## 4. Integration Test Plan

| Test ID | Components           | Test Description                                   | Expected Result                       |
|---------|----------------------|----------------------------------------------------|---------------------------------------|
| I-001   | Server + DB          | Register user, verify in SQLite                    | User row created with hashed password |
| I-002   | Server + DB          | Create camera, verify stream_key generated         | Camera row with UUID stream_key       |
| I-003   | Server + WebSocket   | Connect pusher, join room, relay offer             | Signaling messages relayed correctly  |
| I-004   | Pusher + Server      | Pusher authenticates and starts signaling          | WebSocket connected, room joined      |
| I-005   | Puller + Server      | Puller authenticates and receives stream info      | Stream URL and key obtained           |
| I-006   | Puller + Storage     | Receive mock packets and write MP4 file            | Valid MP4 file on disk                |

## 5. End-to-End Test Plan

| Test ID | Description                                    | Expected Result                               |
|---------|------------------------------------------------|-----------------------------------------------|
| E-001   | Full pipeline: register -> create camera -> push -> pull -> verify recording | MP4 file on disk, valid and playable |
| E-002   | Multiple cameras from same user                | All streams handled independently             |
| E-003   | Server restart with persistent data            | Users and cameras survive restart             |
| E-004   | Pusher disconnect and reconnect                | Stream recovers after reconnection            |
| E-005   | Puller storage rotation                        | Multiple MP4 files created on rotation        |

## 6. Performance Test Metrics

### Target Metrics (Raspberry Pi 5)

| Metric                      | Target              | Measurement Method                  |
|-----------------------------|---------------------|-------------------------------------|
| End-to-end video latency    | < 500ms             | Timestamp comparison (NTP synced)   |
| Pusher CPU usage            | < 30% (single core) | `top` / `pidstat` during streaming  |
| Puller CPU usage            | < 25% (single core) | `top` / `pidstat` during streaming  |
| Server CPU usage            | < 10% (single core) | `top` / `pidstat` during signaling  |
| Pusher memory usage         | < 100 MB            | `ps aux` RSS measurement            |
| Puller memory usage         | < 100 MB            | `ps aux` RSS measurement            |
| Server memory usage         | < 150 MB            | `ps aux` RSS measurement            |
| Video bitrate (1080p)       | 2-6 Mbps            | Network traffic measurement         |
| Video bitrate (720p)        | 1-3 Mbps            | Network traffic measurement         |
| API response time           | < 100ms (p95)       | curl timing in test script          |
| WebSocket signaling latency | < 50ms              | Timestamp measurement               |
| Storage write throughput    | >= 10 MB/s          | File size / duration                |
| Max concurrent cameras      | >= 4                | Load test with multiple pushers     |

### Performance Test Procedure
1. Start server and measure idle resource usage (baseline)
2. Start one pusher at 1080p and measure CPU/memory for 60 seconds
3. Start one puller and measure CPU/memory for 60 seconds
4. Measure end-to-end latency using embedded timestamps
5. Repeat with 2, 3, 4 concurrent cameras
6. Record network bandwidth usage per stream
7. Verify recording file integrity after extended run (30 minutes)

## 7. Test Execution Schedule

| Phase            | Tests      | Priority | Prerequisite                    |
|------------------|------------|----------|---------------------------------|
| Phase 1          | S-001~S-017| High     | Server implementation complete  |
| Phase 2          | P-001~P-006| High     | Pusher build system ready       |
| Phase 3          | R-001~R-006| High     | Puller build system ready       |
| Phase 4          | I-001~I-006| Medium   | Server + one client ready       |
| Phase 5          | E-001~E-005| Medium   | All components ready            |
| Phase 6          | Performance| Low      | E2E tests passing               |
