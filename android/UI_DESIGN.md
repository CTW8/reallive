# Android UI Design (Native Player)

## Navigation

- Dashboard
- Events
- Watch
- Profile

## Watch Screen

### Top Video Region

- Native render surface: `PlayerSurfaceView`
- Mode chip: `LIVE` / `HISTORY`
- Status metrics: fps, bitrate, latency, reconnect state

### Timeline Region

- thumbnail rail
- time ticks
- event markers (person-detected)
- interactions:
  - drag to seek
  - tap event marker to jump
  - "Back to Live"

### Controls

- play/pause (history)
- seek preset (-10s / +10s)
- quality selector (future)

## Dashboard Card

- camera thumbnail
- online/streaming/offline dot
- latest event time
- tap to open watch

## Implementation Steps

1. Replace current debug activity with Dashboard + Watch two-screen flow.
2. Add socket-driven camera status updates.
3. Bind timeline APIs and render event markers.
4. Integrate watch session lifecycle (`start/heartbeat/stop`).
5. Integrate FFmpeg demux and decode to GLES textures.
