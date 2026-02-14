const FLV_BASE_URL = process.env.SRS_FLV_BASE || 'http://localhost:8080';
const RECONNECT_DELAY_MS = 1500;
const TELEMETRY_HISTORY_LIMIT = 120;
const SEI_CACHE_STALE_MS = Math.max(1000, Number(process.env.SEI_CACHE_STALE_MS || 300000));

const TELEMETRY_SEI_UUID = Buffer.from([
  0x52, 0x65, 0x61, 0x4c, 0x69, 0x76, 0x65, 0x53,
  0x65, 0x69, 0x4d, 0x65, 0x74, 0x72, 0x69, 0x63,
]);

const monitors = new Map();
const seiCache = new Map();

function toFiniteNumber(value, fallback = null) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function clampPercent(value) {
  const n = toFiniteNumber(value, 0);
  if (n < 0) return 0;
  if (n > 100) return 100;
  return Math.round(n * 10) / 10;
}

function normalizeTelemetry(device = {}) {
  return {
    cpuPct: clampPercent(device.cpu_pct),
    memoryPct: clampPercent(device.mem_pct),
    storagePct: clampPercent(device.storage_pct),
    memoryUsedMb: Math.round((toFiniteNumber(device.mem_used_mb, 0) || 0) * 10) / 10,
    memoryTotalMb: Math.round((toFiniteNumber(device.mem_total_mb, 0) || 0) * 10) / 10,
    storageUsedGb: Math.round((toFiniteNumber(device.storage_used_gb, 0) || 0) * 100) / 100,
    storageTotalGb: Math.round((toFiniteNumber(device.storage_total_gb, 0) || 0) * 100) / 100,
  };
}

function extractSeiJson(rbsp) {
  let offset = 0;
  while (offset < rbsp.length) {
    if (offset === rbsp.length - 1 && rbsp[offset] === 0x80) {
      break;
    }

    let payloadType = 0;
    while (offset < rbsp.length && rbsp[offset] === 0xff) {
      payloadType += 0xff;
      offset += 1;
    }
    if (offset >= rbsp.length) break;
    payloadType += rbsp[offset];
    offset += 1;

    let payloadSize = 0;
    while (offset < rbsp.length && rbsp[offset] === 0xff) {
      payloadSize += 0xff;
      offset += 1;
    }
    if (offset >= rbsp.length) break;
    payloadSize += rbsp[offset];
    offset += 1;

    if (payloadSize <= 0 || offset + payloadSize > rbsp.length) break;
    const payload = rbsp.subarray(offset, offset + payloadSize);
    offset += payloadSize;

    if (payloadType !== 5 || payload.length < 16) {
      continue;
    }

    const uuid = payload.subarray(0, 16);
    if (!uuid.equals(TELEMETRY_SEI_UUID)) {
      continue;
    }

    const jsonText = payload.subarray(16).toString('utf8').replace(/\u0000+$/g, '');
    try {
      const parsed = JSON.parse(jsonText);
      if (parsed && typeof parsed === 'object') {
        return parsed;
      }
    } catch {
      return null;
    }
  }
  return null;
}

function ebspToRbsp(ebsp) {
  const out = [];
  for (let i = 0; i < ebsp.length; i += 1) {
    if (
      i >= 2 &&
      ebsp[i] === 0x03 &&
      ebsp[i - 1] === 0x00 &&
      ebsp[i - 2] === 0x00
    ) {
      continue;
    }
    out.push(ebsp[i]);
  }
  return Buffer.from(out);
}

function findStartCode(data, from) {
  for (let i = from; i + 3 < data.length; i += 1) {
    if (data[i] === 0x00 && data[i + 1] === 0x00) {
      if (data[i + 2] === 0x01) return { index: i, len: 3 };
      if (data[i + 2] === 0x00 && data[i + 3] === 0x01) return { index: i, len: 4 };
    }
  }
  return null;
}

function parseNaluForSei(nalu) {
  if (!nalu || nalu.length < 2) return null;
  const nalType = nalu[0] & 0x1f;
  if (nalType !== 6) return null;
  const rbsp = ebspToRbsp(nalu.subarray(1));
  return extractSeiJson(rbsp);
}

class FlvSeiParser {
  constructor(onSeiJson) {
    this.onSeiJson = onSeiJson;
    this.buffer = Buffer.alloc(0);
    this.headerParsed = false;
    this.naluLengthSize = 4;
  }

  push(chunk) {
    if (!chunk || chunk.length === 0) return;
    this.buffer = this.buffer.length
      ? Buffer.concat([this.buffer, chunk])
      : Buffer.from(chunk);
    this.parse();
  }

  parse() {
    let offset = 0;
    if (!this.headerParsed) {
      if (this.buffer.length < 13) return;
      if (this.buffer.toString('ascii', 0, 3) !== 'FLV') {
        this.buffer = Buffer.alloc(0);
        return;
      }
      const dataOffset = this.buffer.readUInt32BE(5);
      const need = dataOffset + 4;
      if (this.buffer.length < need) return;
      offset = need;
      this.headerParsed = true;
    }

    while (this.buffer.length - offset >= 15) {
      const tagType = this.buffer[offset];
      const dataSize =
        (this.buffer[offset + 1] << 16) |
        (this.buffer[offset + 2] << 8) |
        this.buffer[offset + 3];
      const fullSize = 11 + dataSize + 4;
      if (this.buffer.length - offset < fullSize) break;

      if (tagType === 9 && dataSize > 0) {
        const tagData = this.buffer.subarray(offset + 11, offset + 11 + dataSize);
        this.parseVideoTag(tagData);
      }

      offset += fullSize;
    }

    if (offset > 0) {
      this.buffer = this.buffer.subarray(offset);
    }
    if (this.buffer.length > 2 * 1024 * 1024) {
      this.buffer = this.buffer.subarray(this.buffer.length - 1024 * 1024);
    }
  }

  parseVideoTag(data) {
    if (data.length < 5) return;
    const codecId = data[0] & 0x0f;
    if (codecId !== 7) return;
    const avcPacketType = data[1];
    if (avcPacketType === 0) {
      this.parseAvcConfig(data.subarray(5));
      return;
    }
    if (avcPacketType !== 1) return;

    const payload = data.subarray(5);
    if (!this.parseAvccNalus(payload)) {
      this.parseAnnexBNalus(payload);
    }
  }

  parseAvcConfig(config) {
    if (!config || config.length < 7) return;
    this.naluLengthSize = (config[4] & 0x03) + 1;
    if (this.naluLengthSize < 1 || this.naluLengthSize > 4) {
      this.naluLengthSize = 4;
    }
  }

  readNaluSize(payload, offset) {
    const len = this.naluLengthSize || 4;
    if (offset + len > payload.length) return null;
    let size = 0;
    for (let i = 0; i < len; i += 1) {
      size = (size << 8) | payload[offset + i];
    }
    return size;
  }

  parseAvccNalus(payload) {
    let offset = 0;
    let foundAny = false;
    const len = this.naluLengthSize || 4;
    while (offset + len <= payload.length) {
      const naluSize = this.readNaluSize(payload, offset);
      if (naluSize == null) return false;
      offset += len;
      if (naluSize <= 0 || offset + naluSize > payload.length) {
        return false;
      }
      foundAny = true;
      const nalu = payload.subarray(offset, offset + naluSize);
      const sei = parseNaluForSei(nalu);
      if (sei) this.onSeiJson(sei);
      offset += naluSize;
    }
    return foundAny && offset === payload.length;
  }

  parseAnnexBNalus(payload) {
    let pos = 0;
    while (true) {
      const start = findStartCode(payload, pos);
      if (!start) break;
      const next = findStartCode(payload, start.index + start.len);
      const naluStart = start.index + start.len;
      const naluEnd = next ? next.index : payload.length;
      if (naluEnd > naluStart) {
        const nalu = payload.subarray(naluStart, naluEnd);
        const sei = parseNaluForSei(nalu);
        if (sei) this.onSeiJson(sei);
      }
      if (!next) break;
      pos = next.index;
    }
  }
}

function updateSeiCache(streamKey, payload) {
  if (!payload || typeof payload !== 'object') return;
  const now = Date.now();
  const item = seiCache.get(streamKey) || {
    updatedAt: 0,
    telemetry: null,
    telemetryHistory: [],
    cameraConfig: null,
    configurable: null,
  };

  if (payload.device && typeof payload.device === 'object') {
    const telemetry = normalizeTelemetry(payload.device);
    item.telemetry = telemetry;
    item.telemetryHistory.push({
      ts: toFiniteNumber(payload.ts, now),
      cpuPct: telemetry.cpuPct,
      memoryPct: telemetry.memoryPct,
      storagePct: telemetry.storagePct,
    });
    if (item.telemetryHistory.length > TELEMETRY_HISTORY_LIMIT) {
      item.telemetryHistory.splice(0, item.telemetryHistory.length - TELEMETRY_HISTORY_LIMIT);
    }
    if (item.telemetryHistory.length === 1) {
      console.log(`[SEI Monitor] stream=${streamKey} telemetry online`);
    }
  }

  if (payload.camera && typeof payload.camera === 'object') {
    item.cameraConfig = payload.camera;
  }
  if (payload.configurable && typeof payload.configurable === 'object') {
    item.configurable = payload.configurable;
  }

  item.updatedAt = now;
  seiCache.set(streamKey, item);
}

async function runMonitorLoop(streamKey, state) {
  const app = state.app || 'live';
  const url = `${FLV_BASE_URL}/${encodeURIComponent(app)}/${encodeURIComponent(streamKey)}.flv`;
  while (state.running) {
    try {
      state.abortController = new AbortController();
      const response = await fetch(url, { signal: state.abortController.signal });
      if (!response.ok || !response.body) {
        throw new Error(`HTTP ${response.status}`);
      }

      const parser = new FlvSeiParser((payload) => updateSeiCache(streamKey, payload));
      const reader = response.body.getReader();
      while (state.running) {
        const { done, value } = await reader.read();
        if (done) {
          throw new Error('FLV stream closed');
        }
        if (value && value.length) {
          parser.push(Buffer.from(value));
        }
      }
    } catch (err) {
      if (!state.running) break;
      if (err?.name !== 'AbortError') {
        console.error(`[SEI Monitor] stream=${streamKey} ${err.message}`);
      }
    } finally {
      state.abortController = null;
    }

    if (state.running) {
      await new Promise((resolve) => setTimeout(resolve, RECONNECT_DELAY_MS));
    }
  }
}

function startSeiMonitor(streamKey, app = 'live') {
  if (!streamKey) return;
  const existing = monitors.get(streamKey);
  if (existing) {
    if ((existing.app || 'live') === app) {
      return;
    }
    stopSeiMonitor(streamKey);
  }

  const state = { running: true, abortController: null, app };
  monitors.set(streamKey, state);
  runMonitorLoop(streamKey, state);
}

function stopSeiMonitor(streamKey) {
  const state = monitors.get(streamKey);
  if (!state) return;
  state.running = false;
  if (state.abortController) {
    state.abortController.abort();
  }
  monitors.delete(streamKey);
}

function stopAllSeiMonitors() {
  for (const key of monitors.keys()) {
    stopSeiMonitor(key);
  }
}

function getSeiInfo(streamKey) {
  const value = seiCache.get(streamKey);
  if (!value) return null;
  if (Date.now() - Number(value.updatedAt || 0) > SEI_CACHE_STALE_MS) {
    seiCache.delete(streamKey);
    return null;
  }
  return {
    updatedAt: value.updatedAt,
    telemetry: value.telemetry ? { ...value.telemetry } : null,
    telemetryHistory: value.telemetryHistory.slice(),
    cameraConfig: value.cameraConfig ? { ...value.cameraConfig } : null,
    configurable: value.configurable ? { ...value.configurable } : null,
  };
}

module.exports = {
  startSeiMonitor,
  stopSeiMonitor,
  stopAllSeiMonitors,
  getSeiInfo,
};
