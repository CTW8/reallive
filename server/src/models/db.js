const Database = require('better-sqlite3');
const path = require('path');
const fs = require('fs');
const config = require('../config');

// Ensure data directory exists
const dataDir = path.dirname(config.dbPath);
if (!fs.existsSync(dataDir)) {
  fs.mkdirSync(dataDir, { recursive: true });
}

const db = new Database(config.dbPath);

// Enable WAL mode for better concurrent performance
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');

// Initialize schema
db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    role TEXT DEFAULT 'viewer',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  );

  CREATE TABLE IF NOT EXISTS cameras (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    stream_key TEXT UNIQUE NOT NULL,
    status TEXT DEFAULT 'offline',
    resolution TEXT DEFAULT '1080p',
    location TEXT,
    ip_address TEXT,
    model TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
  );

  CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id INTEGER NOT NULL,
    start_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    end_time DATETIME,
    status TEXT DEFAULT 'active',
    FOREIGN KEY (camera_id) REFERENCES cameras(id) ON DELETE CASCADE
  );

  CREATE TABLE IF NOT EXISTS alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    camera_id INTEGER,
    type TEXT NOT NULL,
    title TEXT NOT NULL,
    description TEXT,
    status TEXT DEFAULT 'new',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    resolved_at DATETIME,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (camera_id) REFERENCES cameras(id) ON DELETE SET NULL
  );

  CREATE TABLE IF NOT EXISTS alert_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    priority TEXT DEFAULT 'medium',
    condition TEXT NOT NULL,
    actions TEXT NOT NULL,
    escalation TEXT DEFAULT 'Immediately',
    quiet_hours TEXT DEFAULT 'Disabled',
    enabled INTEGER DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
  );

  CREATE TABLE IF NOT EXISTS audit_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    action TEXT NOT NULL,
    type TEXT DEFAULT 'config',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
  );

  CREATE TABLE IF NOT EXISTS user_settings (
    user_id INTEGER PRIMARY KEY,
    profile_json TEXT NOT NULL DEFAULT '{}',
    notification_json TEXT NOT NULL DEFAULT '{}',
    system_json TEXT NOT NULL DEFAULT '{}',
    security_json TEXT NOT NULL DEFAULT '{}',
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
  );

  CREATE TABLE IF NOT EXISTS camera_settings (
    camera_id INTEGER PRIMARY KEY,
    location TEXT DEFAULT '',
    motion_enabled INTEGER DEFAULT 1,
    motion_sensitivity TEXT DEFAULT 'High',
    person_enabled INTEGER DEFAULT 1,
    sound_enabled INTEGER DEFAULT 0,
    sound_sensitivity TEXT DEFAULT 'Loud',
    detection_zones TEXT DEFAULT '2 zones configured',
    night_vision_enabled INTEGER DEFAULT 1,
    night_vision_mode TEXT DEFAULT 'Auto',
    image_flip_mode TEXT DEFAULT 'Normal',
    watermark_enabled INTEGER DEFAULT 1,
    stream_profile TEXT DEFAULT 'auto',
    stream_mode TEXT DEFAULT 'auto',
    manual_level INTEGER DEFAULT 2,
    auto_min_level INTEGER DEFAULT 0,
    auto_max_level INTEGER DEFAULT 4,
    auto_policy TEXT DEFAULT 'balanced',
    auto_cooldown_sec INTEGER DEFAULT 10,
    auto_up_hold_sec INTEGER DEFAULT 25,
    auto_down_hold_sec INTEGER DEFAULT 3,
    firmware_version TEXT DEFAULT 'v2.3.8',
    firmware_update_available INTEGER DEFAULT 1,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (camera_id) REFERENCES cameras(id) ON DELETE CASCADE
  );
`);

function ensureColumn(table, column, definition) {
  const cols = db.prepare(`PRAGMA table_info(${table})`).all();
  const exists = cols.some((it) => String(it.name) === column);
  if (!exists) {
    db.exec(`ALTER TABLE ${table} ADD COLUMN ${column} ${definition}`);
  }
}

ensureColumn('camera_settings', 'stream_mode', "TEXT DEFAULT 'auto'");
ensureColumn('camera_settings', 'stream_profile', "TEXT DEFAULT 'auto'");
ensureColumn('camera_settings', 'manual_level', 'INTEGER DEFAULT 2');
ensureColumn('camera_settings', 'auto_min_level', 'INTEGER DEFAULT 0');
ensureColumn('camera_settings', 'auto_max_level', 'INTEGER DEFAULT 4');
ensureColumn('camera_settings', 'auto_policy', "TEXT DEFAULT 'balanced'");
ensureColumn('camera_settings', 'auto_cooldown_sec', 'INTEGER DEFAULT 10');
ensureColumn('camera_settings', 'auto_up_hold_sec', 'INTEGER DEFAULT 25');
ensureColumn('camera_settings', 'auto_down_hold_sec', 'INTEGER DEFAULT 3');

module.exports = db;
