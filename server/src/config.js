const fs = require('fs');
const path = require('path');

const SERVER_ROOT = path.join(__dirname, '..');
const DEFAULT_CONFIG_PATH = path.join(SERVER_ROOT, 'config', 'server.json');
const CONFIG_PATH = process.env.SERVER_CONFIG_PATH || DEFAULT_CONFIG_PATH;

const defaults = {
  port: 3000,
  jwtSecret: 'reallive-dev-secret-change-in-production',
  jwtExpiresIn: '24h',
  bcryptRounds: 12,
  dbPath: path.join(SERVER_ROOT, 'data', 'reallive.db'),
  srsApi: 'http://localhost:1985',
  edgeReplay: {
    url: '',
    timeoutMs: 1500,
  },
};

function loadFileConfig(filePath) {
  try {
    if (!fs.existsSync(filePath)) return {};
    const raw = fs.readFileSync(filePath, 'utf8');
    const json = JSON.parse(raw);
    return json && typeof json === 'object' ? json : {};
  } catch (err) {
    console.error('[Config] Failed to parse config file:', filePath, err.message);
    return {};
  }
}

function toNumber(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

const fileConfig = loadFileConfig(CONFIG_PATH);
const mergedEdgeReplay = {
  ...defaults.edgeReplay,
  ...(fileConfig.edgeReplay || {}),
};

const config = {
  ...defaults,
  ...fileConfig,
  edgeReplay: mergedEdgeReplay,
};

if (process.env.PORT) config.port = toNumber(process.env.PORT, config.port);
if (process.env.JWT_SECRET) config.jwtSecret = process.env.JWT_SECRET;
if (process.env.DB_PATH) config.dbPath = process.env.DB_PATH;
if (process.env.SRS_API) config.srsApi = process.env.SRS_API;
if (process.env.EDGE_REPLAY_URL) config.edgeReplay.url = process.env.EDGE_REPLAY_URL;
if (process.env.EDGE_REPLAY_TIMEOUT_MS) {
  config.edgeReplay.timeoutMs = toNumber(process.env.EDGE_REPLAY_TIMEOUT_MS, config.edgeReplay.timeoutMs);
}

if (typeof config.dbPath === 'string' && !path.isAbsolute(config.dbPath)) {
  config.dbPath = path.resolve(SERVER_ROOT, config.dbPath);
}

module.exports = config;
