const FLV_BASE_URL = process.env.SRS_FLV_BASE || 'http://localhost:8080';
const RECONNECT_DELAY_MS = 1500;
const TELEMETRY_HISTORY_LIMIT = 120;
const PERSON_EVENTS_LIMIT = 200;
const SEI_CACHE_STALE_MS = Math.max(1000, Number(process.env.SEI_CACHE_STALE_MS || 300000));
const SEI_MONITOR_BUILD_TAG = 'sei-monitor-v5';
const EPOCH_MS_MIN = 946684800000;   // 2000-01-01
const EPOCH_MS_MAX = 4102444800000;  // 2100-01-01
const SEI_DEBUG_BUILD_TAG = 'sei-debug-v6';

const TELEMETRY_SEI_UUID = Buffer.from([
  0x52, 0x65, 0x61, 0x4c, 0x69, 0x76, 0x65, 0x53,
  0x65, 0x69, 0x4d, 0x65, 0x74, 0x72, 0x69, 0x63,
]);

const monitors = new Map();
const seiCache = new Map();
let seiEventEmitter = null;

function normalizeStreamKey(value) {
  const raw = String(value || '').trim();
  if (!raw) return '';
  const noQuery = raw.split('?')[0].split('#')[0];
  const last = noQuery.includes('/') ? noQuery.slice(noQuery.lastIndexOf('/') + 1) : noQuery;
  if (last.toLowerCase().endsWith('.flv')) {
    return last.slice(0, -4);
  }
  return last;
}

function toFiniteNumber(value, fallback = null) {
  if (typeof value === 'string') {
    const raw = value.trim().toLowerCase();
    if (!raw) return fallback;
    const m = raw.match(/[-+]?\d+(\.\d+)?/);
    if (m) {
      const parsed = Number(m[0]);
      if (Number.isFinite(parsed)) {
        // Unit-aware coercion for bitrate strings.
        if (raw.includes('mbps')) return parsed * 1000;
        if (raw.includes('kbps')) return parsed;
        if (raw.includes('bps')) return parsed / 1000;
        return parsed;
      }
    }
    return fallback;
  }
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
  const firstFinite = (...values) => {
    for (const value of values) {
      const n = toFiniteNumber(value, null);
      if (Number.isFinite(n)) return n;
    }
    return null;
  };

  const cpuCorePctRaw = Array.isArray(device.cpu_core_pct) ? device.cpu_core_pct : [];
  const cpuCorePct = cpuCorePctRaw
    .map((value) => clampPercent(value))
    .filter((value) => Number.isFinite(value));
  const streamOutFps = firstFinite(
    device.stream_out_fps,
    device.streamOutFps,
    device.out_fps,
    device.outFps,
    device.stream_fps,
    device.streamFps,
    device.video_fps,
    device.videoFps,
    device.fps,
  );
  const streamOutBitrateBps = firstFinite(
    device.stream_out_bitrate_bps,
    device.streamOutBitrateBps,
    device.out_bitrate_bps,
    device.outBitrateBps,
    device.stream_bitrate_bps,
    device.streamBitrateBps,
    device.video_bitrate_bps,
    device.videoBitrateBps,
    device.bitrate_bps,
    device.bitrateBps,
  );
  const streamOutBitrateKbpsRaw = firstFinite(
    device.stream_out_bitrate_kbps,
    device.streamOutBitrateKbps,
    device.out_bitrate_kbps,
    device.outBitrateKbps,
    device.stream_bitrate_kbps,
    device.streamBitrateKbps,
    device.video_bitrate_kbps,
    device.videoBitrateKbps,
    device.bitrate_kbps,
    device.bitrateKbps,
    device.kbps,
  );
  const streamOutBitrateKbps = Number.isFinite(streamOutBitrateKbpsRaw)
    ? streamOutBitrateKbpsRaw
    : (Number.isFinite(streamOutBitrateBps) ? (streamOutBitrateBps / 1000) : null);

  return {
    cpuPct: clampPercent(device.cpu_pct),
    cpuCorePct,
    memoryPct: clampPercent(device.mem_pct),
    storagePct: clampPercent(device.storage_pct),
    memoryUsedMb: Math.round((toFiniteNumber(device.mem_used_mb, 0) || 0) * 10) / 10,
    memoryTotalMb: Math.round((toFiniteNumber(device.mem_total_mb, 0) || 0) * 10) / 10,
    storageUsedGb: Math.round((toFiniteNumber(device.storage_used_gb, 0) || 0) * 100) / 100,
    storageTotalGb: Math.round((toFiniteNumber(device.storage_total_gb, 0) || 0) * 100) / 100,
    streamOutFps: Number.isFinite(streamOutFps) ? Math.round(streamOutFps * 100) / 100 : null,
    streamOutBitrateBps: Number.isFinite(streamOutBitrateBps) ? Math.max(0, Math.round(streamOutBitrateBps)) : null,
    streamOutBitrateKbps: Number.isFinite(streamOutBitrateKbps)
      ? Math.round(streamOutBitrateKbps * 10) / 10
      : null,
  };
}

function findNumericMetricDeep(root = {}, keys = [], maxDepth = 5) {
  if (!root || typeof root !== 'object') return null;
  const targets = new Set(keys.map((k) => String(k).toLowerCase()));
  const queue = [{ value: root, depth: 0 }];
  while (queue.length) {
    const node = queue.shift();
    if (!node || !node.value || typeof node.value !== 'object') continue;
    if (node.depth > maxDepth) continue;
    for (const [k, v] of Object.entries(node.value)) {
      const key = String(k).toLowerCase();
      if (targets.has(key)) {
        const n = toFiniteNumber(v, null);
        if (Number.isFinite(n)) return n;
      }
      if (v && typeof v === 'object') {
        queue.push({ value: v, depth: node.depth + 1 });
      }
    }
  }
  return null;
}

function enrichTelemetryFromPayload(payload = {}, telemetry = {}) {
  const enriched = { ...telemetry };
  if (!Number.isFinite(enriched.streamOutFps) || enriched.streamOutFps <= 0) {
    const fps = findNumericMetricDeep(payload, [
      'stream_out_fps', 'streamoutfps', 'out_fps', 'outfps',
      'stream_fps', 'streamfps', 'video_fps', 'videofps', 'fps',
    ]);
    if (Number.isFinite(fps) && fps > 0) {
      enriched.streamOutFps = Math.round(fps * 100) / 100;
    }
  }

  if (
    (!Number.isFinite(enriched.streamOutBitrateKbps) || enriched.streamOutBitrateKbps <= 0) &&
    (!Number.isFinite(enriched.streamOutBitrateBps) || enriched.streamOutBitrateBps <= 0)
  ) {
    const kbps = findNumericMetricDeep(payload, [
      'stream_out_bitrate_kbps', 'streamoutbitratekbps', 'out_bitrate_kbps', 'outbitratekbps',
      'stream_bitrate_kbps', 'streambitratekbps', 'video_bitrate_kbps', 'videobitratekbps',
      'bitrate_kbps', 'bitratekbps', 'kbps',
    ]);
    const bps = findNumericMetricDeep(payload, [
      'stream_out_bitrate_bps', 'streamoutbitratebps', 'out_bitrate_bps', 'outbitratebps',
      'stream_bitrate_bps', 'streambitratebps', 'video_bitrate_bps', 'videobitratebps',
      'bitrate_bps', 'bitratebps', 'bps',
    ]);
    const genericBitrate = findNumericMetricDeep(payload, [
      'stream_out_bitrate', 'streamoutbitrate', 'out_bitrate', 'outbitrate',
      'stream_bitrate', 'streambitrate', 'video_bitrate', 'videobitrate',
      'bitrate',
    ]);
    if (Number.isFinite(kbps) && kbps > 0) {
      enriched.streamOutBitrateKbps = Math.round(kbps * 10) / 10;
      enriched.streamOutBitrateBps = Math.round(kbps * 1000);
    } else if (Number.isFinite(bps) && bps > 0) {
      enriched.streamOutBitrateBps = Math.round(bps);
      enriched.streamOutBitrateKbps = Math.round((bps / 1000) * 10) / 10;
    } else if (Number.isFinite(genericBitrate) && genericBitrate > 0) {
      // Heuristic: large values are usually bps, smaller ones are kbps.
      if (genericBitrate >= 200000) {
        enriched.streamOutBitrateBps = Math.round(genericBitrate);
        enriched.streamOutBitrateKbps = Math.round((genericBitrate / 1000) * 10) / 10;
      } else {
        enriched.streamOutBitrateKbps = Math.round(genericBitrate * 10) / 10;
        enriched.streamOutBitrateBps = Math.round(genericBitrate * 1000);
      }
    }
  }

  // Last fallback for legacy payloads: camera configured values.
  if (!Number.isFinite(enriched.streamOutFps) || enriched.streamOutFps <= 0) {
    const cfgFps = findNumericMetricDeep(payload, ['camera_fps', 'camerafps', 'fps']);
    if (Number.isFinite(cfgFps) && cfgFps > 0) {
      enriched.streamOutFps = Math.round(cfgFps * 100) / 100;
    }
  }
  if (
    (!Number.isFinite(enriched.streamOutBitrateKbps) || enriched.streamOutBitrateKbps <= 0) &&
    (!Number.isFinite(enriched.streamOutBitrateBps) || enriched.streamOutBitrateBps <= 0)
  ) {
    const cfgBitrate = findNumericMetricDeep(payload, [
      'camera_bitrate', 'camerabitrate', 'bitrate',
    ]);
    if (Number.isFinite(cfgBitrate) && cfgBitrate > 0) {
      if (cfgBitrate >= 200000) {
        enriched.streamOutBitrateBps = Math.round(cfgBitrate);
        enriched.streamOutBitrateKbps = Math.round((cfgBitrate / 1000) * 10) / 10;
      } else {
        enriched.streamOutBitrateKbps = Math.round(cfgBitrate * 10) / 10;
        enriched.streamOutBitrateBps = Math.round(cfgBitrate * 1000);
      }
    }
  }
  return enriched;
}

function hasOwn(obj, key) {
  return Object.prototype.hasOwnProperty.call(obj, key);
}

function hasAnyTelemetryField(obj = {}) {
  if (!obj || typeof obj !== 'object') return false;
  const keys = [
    'cpu_pct', 'mem_pct', 'storage_pct',
    'stream_out_fps', 'streamOutFps', 'out_fps', 'outFps', 'stream_fps', 'streamFps', 'video_fps', 'videoFps', 'fps',
    'stream_out_bitrate_bps', 'streamOutBitrateBps', 'out_bitrate_bps', 'outBitrateBps', 'stream_bitrate_bps', 'streamBitrateBps', 'video_bitrate_bps', 'videoBitrateBps', 'bitrate_bps', 'bitrateBps',
    'stream_out_bitrate_kbps', 'streamOutBitrateKbps', 'out_bitrate_kbps', 'outBitrateKbps', 'stream_bitrate_kbps', 'streamBitrateKbps', 'video_bitrate_kbps', 'videoBitrateKbps', 'bitrate_kbps', 'bitrateKbps', 'kbps',
  ];
  return keys.some((k) => hasOwn(obj, k));
}

function findTelemetryObject(payload = {}) {
  if (!payload || typeof payload !== 'object') return null;
  const candidates = [
    payload.device,
    payload.telemetry,
    payload.stats && payload.stats.device,
    payload.stats && payload.stats.telemetry,
    payload.stats,
    payload.stream,
    payload.video,
    payload,
  ];
  for (const candidate of candidates) {
    if (candidate && typeof candidate === 'object' && hasAnyTelemetryField(candidate)) {
      return candidate;
    }
  }
  return null;
}

function clamp01(value) {
  const n = toFiniteNumber(value, 0);
  if (n < 0) return 0;
  if (n > 1) return 1;
  return Math.round(n * 1000) / 1000;
}

function normalizeTimestampMs(value, fallbackTs) {
  const n = toFiniteNumber(value, null);
  if (!Number.isFinite(n) || n <= 0) return fallbackTs;
  if (n >= EPOCH_MS_MIN && n <= EPOCH_MS_MAX) return Math.floor(n); // ms
  if (n >= EPOCH_MS_MIN / 1000 && n <= EPOCH_MS_MAX / 1000) return Math.floor(n * 1000); // s
  if (n >= EPOCH_MS_MIN * 1000 && n <= EPOCH_MS_MAX * 1000) return Math.floor(n / 1000); // us
  if (n >= EPOCH_MS_MIN * 1000000 && n <= EPOCH_MS_MAX * 1000000) return Math.floor(n / 1000000); // ns
  return fallbackTs;
}

function normalizeBBox(bbox = {}) {
  const x = Math.max(0, Math.floor(toFiniteNumber(bbox.x, 0) || 0));
  const y = Math.max(0, Math.floor(toFiniteNumber(bbox.y, 0) || 0));
  const w = Math.max(0, Math.floor(toFiniteNumber(bbox.w, 0) || 0));
  const h = Math.max(0, Math.floor(toFiniteNumber(bbox.h, 0) || 0));
  return { x, y, w, h };
}

function normalizePersonState(person = {}, fallbackTs) {
  const bbox = normalizeBBox(person.bbox || {});
  const active = Boolean(person.active) && bbox.w > 0 && bbox.h > 0;
  return {
    active,
    score: clamp01(person.score),
    ts: normalizeTimestampMs(person.ts, fallbackTs),
    bbox,
  };
}

function normalizePersonEvent(event = {}, fallbackTs) {
  const type = String(event.type || '').toLowerCase();
  if (type !== 'person_detected' && type !== 'person') return null;
  const bbox = normalizeBBox(event.bbox || {});
  if (bbox.w <= 0 || bbox.h <= 0) return null;
  return {
    type: 'person-detected',
    ts: normalizeTimestampMs(event.ts, fallbackTs),
    score: clamp01(event.score),
    bbox,
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
  const payloadStreamKey = normalizeStreamKey(payload?.stream_key || payload?.streamKey || '');
  const cacheKey = payloadStreamKey || normalizeStreamKey(streamKey);
  if (!cacheKey) return;

  const item = seiCache.get(cacheKey) || {
    updatedAt: 0,
    telemetry: null,
    telemetryHistory: [],
    telemetrySamples: 0,
    cameraConfig: null,
    configurable: null,
    person: null,
    personEvents: [],
    personEventDedup: new Set(),
  };
  if (!(item.personEventDedup instanceof Set)) {
    item.personEventDedup = new Set(
      (item.personEvents || []).map(
        (evt) => `${evt.type}:${evt.ts}:${evt.bbox?.x}:${evt.bbox?.y}:${evt.bbox?.w}:${evt.bbox?.h}`
      )
    );
  }
  if (!Number.isFinite(item.telemetrySamples)) {
    item.telemetrySamples = 0;
  }

  const payloadTs = normalizeTimestampMs(payload?.ts, now);
  if (!item.debugTagLogged) {
    console.log(`[SEI Monitor] debug tag=${SEI_DEBUG_BUILD_TAG} stream=${cacheKey}`);
    item.debugTagLogged = true;
  }

  const telemetryObj = findTelemetryObject(payload);
  if (telemetryObj) {
    const telemetry = enrichTelemetryFromPayload(payload, normalizeTelemetry(telemetryObj));
    item.telemetry = telemetry;
    item.telemetrySamples += 1;
    item.telemetryHistory.push({
      ts: payloadTs,
      cpuPct: telemetry.cpuPct,
      cpuCorePct: telemetry.cpuCorePct,
      memoryPct: telemetry.memoryPct,
      storagePct: telemetry.storagePct,
      storageUsedGb: telemetry.storageUsedGb,
      storageTotalGb: telemetry.storageTotalGb,
      streamOutFps: telemetry.streamOutFps,
      streamOutBitrateBps: telemetry.streamOutBitrateBps,
      streamOutBitrateKbps: telemetry.streamOutBitrateKbps,
    });
    if (item.telemetryHistory.length > TELEMETRY_HISTORY_LIMIT) {
      item.telemetryHistory.splice(0, item.telemetryHistory.length - TELEMETRY_HISTORY_LIMIT);
    }
    if (item.telemetryHistory.length === 1) {
      console.log(`[SEI Monitor] stream=${cacheKey} telemetry online`);
    }
    if (item.telemetrySamples % 10 === 0) {
      console.log(
        `[SEI Monitor] stream=${cacheKey} sample=${item.telemetrySamples} out_fps=${telemetry.streamOutFps ?? 'null'} out_kbps=${telemetry.streamOutBitrateKbps ?? 'null'} out_bps=${telemetry.streamOutBitrateBps ?? 'null'}`
      );
    }
    if (!Number.isFinite(telemetry.streamOutFps) && !Number.isFinite(telemetry.streamOutBitrateKbps) && !Number.isFinite(telemetry.streamOutBitrateBps)) {
      const sourceKeys = Object.keys(telemetryObj).slice(0, 24).join(',');
      const topKeys = Object.keys(payload || {}).slice(0, 24).join(',');
      const cameraKeys = payload?.camera && typeof payload.camera === 'object'
        ? Object.keys(payload.camera).slice(0, 24).join(',')
        : '';
      const deviceKeys = payload?.device && typeof payload.device === 'object'
        ? Object.keys(payload.device).slice(0, 24).join(',')
        : '';
      const streamOutRaw = payload?.device?.stream_out_fps
        ?? payload?.device?.streamOutFps
        ?? payload?.telemetry?.stream_out_fps
        ?? payload?.telemetry?.streamOutFps
        ?? null;
      const bitrateRaw = payload?.device?.stream_out_bitrate_kbps
        ?? payload?.device?.streamOutBitrateKbps
        ?? payload?.device?.stream_out_bitrate_bps
        ?? payload?.device?.streamOutBitrateBps
        ?? payload?.telemetry?.stream_out_bitrate_kbps
        ?? payload?.telemetry?.streamOutBitrateKbps
        ?? payload?.telemetry?.stream_out_bitrate_bps
        ?? payload?.telemetry?.streamOutBitrateBps
        ?? null;
      console.log(
        `[SEI Monitor] stream=${cacheKey} telemetry missing out-fps/bitrate ` +
        `keys=[${sourceKeys}] top=[${topKeys}] device=[${deviceKeys}] camera=[${cameraKeys}] ` +
        `rawFps=${streamOutRaw ?? 'null'} rawBitrate=${bitrateRaw ?? 'null'}`
      );
    }
  }

  if (payload.camera && typeof payload.camera === 'object') {
    item.cameraConfig = payload.camera;
  }
  if (payload.configurable && typeof payload.configurable === 'object') {
    item.configurable = payload.configurable;
  }

  if (payload.person && typeof payload.person === 'object') {
    item.person = normalizePersonState(payload.person, payloadTs);
  }

  if (Array.isArray(payload.events) && payload.events.length) {
    const emitted = [];
    for (const rawEvent of payload.events) {
      const evt = normalizePersonEvent(rawEvent, payloadTs);
      if (!evt) continue;
      const dedupKey = `${evt.type}:${evt.ts}:${evt.bbox.x}:${evt.bbox.y}:${evt.bbox.w}:${evt.bbox.h}`;
      if (item.personEventDedup.has(dedupKey)) continue;
      item.personEventDedup.add(dedupKey);
      item.personEvents.push(evt);
      emitted.push(evt);
    }

    if (item.personEvents.length > PERSON_EVENTS_LIMIT) {
      item.personEvents.splice(0, item.personEvents.length - PERSON_EVENTS_LIMIT);
    }
    if (item.personEventDedup.size > PERSON_EVENTS_LIMIT * 3) {
      const keep = new Set(
        item.personEvents.map((evt) => `${evt.type}:${evt.ts}:${evt.bbox.x}:${evt.bbox.y}:${evt.bbox.w}:${evt.bbox.h}`)
      );
      item.personEventDedup = keep;
    }

    if (emitted.length && typeof seiEventEmitter === 'function') {
      for (const evt of emitted) {
        try {
          seiEventEmitter(cacheKey, evt);
        } catch (err) {
          console.error('[SEI Monitor] event emit failed:', err?.message || err);
        }
      }
    }
  }

  item.updatedAt = now;
  seiCache.set(cacheKey, item);
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
  console.log(`[SEI Monitor] start stream=${streamKey} app=${app} tag=${SEI_MONITOR_BUILD_TAG}`);
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

function setSeiEventEmitter(emitter) {
  seiEventEmitter = typeof emitter === 'function' ? emitter : null;
}

function getSeiInfo(streamKey) {
  const normalized = normalizeStreamKey(streamKey);
  const value = seiCache.get(normalized) || seiCache.get(String(streamKey || ''));
  if (!value) return null;
  if (Date.now() - Number(value.updatedAt || 0) > SEI_CACHE_STALE_MS) {
    seiCache.delete(streamKey);
    return null;
  }
  return {
    updatedAt: value.updatedAt,
    telemetry: value.telemetry
      ? {
          ...value.telemetry,
          cpuCorePct: Array.isArray(value.telemetry.cpuCorePct) ? value.telemetry.cpuCorePct.slice() : [],
        }
      : null,
    telemetryHistory: value.telemetryHistory.map((item) => ({
      ...item,
      cpuCorePct: Array.isArray(item.cpuCorePct) ? item.cpuCorePct.slice() : [],
    })),
    cameraConfig: value.cameraConfig ? { ...value.cameraConfig } : null,
    configurable: value.configurable ? { ...value.configurable } : null,
    person: value.person ? { ...value.person, bbox: { ...(value.person.bbox || {}) } } : null,
    personEvents: value.personEvents.slice(-50).map((evt) => ({
      ...evt,
      bbox: { ...(evt.bbox || {}) },
    })),
  };
}

module.exports = {
  startSeiMonitor,
  stopSeiMonitor,
  stopAllSeiMonitors,
  setSeiEventEmitter,
  getSeiInfo,
};
