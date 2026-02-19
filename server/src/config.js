const fs = require('fs');
const path = require('path');

const SERVER_ROOT = path.join(__dirname, '..');
const PROJECT_ROOT = path.join(SERVER_ROOT, '..');
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
  streamUrls: {
    pushTemplate: 'rtmp://{host}:1935/live/{streamKey}',
    pullFlvTemplate: '{proto}://{httpHost}/live/{streamKey}.flv',
    pullHlsTemplate: '{proto}://{httpHost}/live/{streamKey}.m3u8',
  },
  recordings: {
    roots: [
      path.join(PROJECT_ROOT, 'recordings'),
      path.join(PROJECT_ROOT, 'pusher', 'recordings'),
      path.join(PROJECT_ROOT, 'pusher', 'build', 'recordings'),
    ],
  },
  mqttControl: {
    enabled: false,
    brokerUrl: 'mqtt://127.0.0.1:1883',
    username: '',
    password: '',
    clientId: '',
    topicPrefix: 'reallive/device',
    commandQos: 1,
    stateQos: 0,
    commandRetain: true,
    stateStaleMs: 12000,
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
const mergedStreamUrls = {
  ...defaults.streamUrls,
  ...(fileConfig.streamUrls || {}),
};
const mergedRecordings = {
  ...defaults.recordings,
  ...(fileConfig.recordings || {}),
};
const mergedMqttControl = {
  ...defaults.mqttControl,
  ...(fileConfig.mqttControl || {}),
};

const config = {
  ...defaults,
  ...fileConfig,
  edgeReplay: mergedEdgeReplay,
  streamUrls: mergedStreamUrls,
  recordings: mergedRecordings,
  mqttControl: mergedMqttControl,
};

if (process.env.PORT) config.port = toNumber(process.env.PORT, config.port);
if (process.env.JWT_SECRET) config.jwtSecret = process.env.JWT_SECRET;
if (process.env.DB_PATH) config.dbPath = process.env.DB_PATH;
if (process.env.SRS_API) config.srsApi = process.env.SRS_API;
if (process.env.EDGE_REPLAY_URL) config.edgeReplay.url = process.env.EDGE_REPLAY_URL;
if (process.env.EDGE_REPLAY_TIMEOUT_MS) {
  config.edgeReplay.timeoutMs = toNumber(process.env.EDGE_REPLAY_TIMEOUT_MS, config.edgeReplay.timeoutMs);
}
if (process.env.MQTT_CONTROL_ENABLED) {
  config.mqttControl.enabled = ['1', 'true', 'yes', 'on'].includes(
    String(process.env.MQTT_CONTROL_ENABLED).toLowerCase()
  );
}
if (process.env.MQTT_BROKER_URL) config.mqttControl.brokerUrl = process.env.MQTT_BROKER_URL;
if (process.env.MQTT_USERNAME) config.mqttControl.username = process.env.MQTT_USERNAME;
if (process.env.MQTT_PASSWORD) config.mqttControl.password = process.env.MQTT_PASSWORD;
if (process.env.MQTT_CLIENT_ID) config.mqttControl.clientId = process.env.MQTT_CLIENT_ID;
if (process.env.MQTT_TOPIC_PREFIX) config.mqttControl.topicPrefix = process.env.MQTT_TOPIC_PREFIX;

if (typeof config.dbPath === 'string' && !path.isAbsolute(config.dbPath)) {
  config.dbPath = path.resolve(SERVER_ROOT, config.dbPath);
}

module.exports = config;
