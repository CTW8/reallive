/**
 * Test setup helpers for the RealLive server.
 *
 * Overrides DB_PATH to a temporary file so tests do not touch the real database.
 * Provides helpers to register a user and obtain a JWT token.
 */

const { randomBytes } = require('node:crypto');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

// Set DB_PATH to a temp file BEFORE requiring any app code
const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'reallive-test-'));
const testDbPath = path.join(tmpDir, 'test.db');
process.env.DB_PATH = testDbPath;

// Now require the app (it will use the temp DB)
const app = require('../src/app');

let server;
let baseUrl;

/**
 * Start the test server on an ephemeral port.
 * Returns the base URL (e.g. "http://127.0.0.1:12345").
 */
function startServer() {
  return new Promise((resolve, reject) => {
    server = app.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      baseUrl = `http://127.0.0.1:${addr.port}`;
      resolve(baseUrl);
    });
    server.on('error', reject);
  });
}

/**
 * Stop the test server and clean up the temp database.
 */
function stopServer() {
  return new Promise((resolve) => {
    if (server) {
      server.close(() => {
        // Clean up temp files
        try {
          fs.rmSync(tmpDir, { recursive: true, force: true });
        } catch (_) {
          // Ignore cleanup errors
        }
        resolve();
      });
    } else {
      resolve();
    }
  });
}

/**
 * Get the current base URL.
 */
function getBaseUrl() {
  return baseUrl;
}

/**
 * Register a new user and return the response JSON.
 */
async function registerUser(username, password, email) {
  const res = await fetch(`${baseUrl}/api/auth/register`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password, email }),
  });
  return { status: res.status, body: await res.json() };
}

/**
 * Login and return the response JSON.
 */
async function loginUser(username, password) {
  const res = await fetch(`${baseUrl}/api/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  return { status: res.status, body: await res.json() };
}

/**
 * Register a user and return their JWT token directly.
 */
async function getToken(username, password, email) {
  const suffix = randomBytes(4).toString('hex');
  const u = username || `testuser_${suffix}`;
  const p = password || 'password123';
  const e = email || `${u}@test.com`;
  const result = await registerUser(u, p, e);
  return { token: result.body.token, user: result.body.user, username: u, password: p, email: e };
}

module.exports = {
  startServer,
  stopServer,
  getBaseUrl,
  registerUser,
  loginUser,
  getToken,
};
