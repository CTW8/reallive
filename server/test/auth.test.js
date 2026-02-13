/**
 * Auth endpoint tests (S-001 to S-006, S-013, S-014)
 *
 * Uses Node.js built-in test runner (node:test) and native fetch.
 */

const { describe, it, before, after } = require('node:test');
const assert = require('node:assert/strict');
const jwt = require('jsonwebtoken');

const { startServer, stopServer, getBaseUrl, registerUser, loginUser, getToken } = require('./setup');

describe('Auth API', () => {
  let base;

  before(async () => {
    base = await startServer();
  });

  after(async () => {
    await stopServer();
  });

  // S-001: Register with valid username/email/password -> 201
  it('S-001: register with valid credentials returns 201', async () => {
    const res = await registerUser('alice', 'secret123', 'alice@example.com');
    assert.equal(res.status, 201);
    assert.ok(res.body.token, 'should return a token');
    assert.equal(res.body.user.username, 'alice');
    assert.equal(res.body.user.email, 'alice@example.com');
    assert.ok(res.body.user.id, 'should return user id');
  });

  // S-002: Register with duplicate username -> 409
  it('S-002: register with duplicate username returns 409', async () => {
    await registerUser('bob', 'password1', 'bob@example.com');
    const res = await registerUser('bob', 'password2', 'bob2@example.com');
    assert.equal(res.status, 409);
    assert.ok(res.body.error);
  });

  // S-003: Register with missing fields -> 400
  it('S-003: register with missing username returns 400', async () => {
    const res = await fetch(`${base}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: 'secret123', email: 'x@y.com' }),
    });
    assert.equal(res.status, 400);
    const body = await res.json();
    assert.ok(body.error);
  });

  it('S-003: register with missing password returns 400', async () => {
    const res = await fetch(`${base}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'nopass', email: 'x@y.com' }),
    });
    assert.equal(res.status, 400);
  });

  it('S-003: register with missing email returns 400', async () => {
    const res = await fetch(`${base}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'noemail', password: 'secret123' }),
    });
    assert.equal(res.status, 400);
  });

  it('S-003: register with short password returns 400', async () => {
    const res = await fetch(`${base}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: 'short', password: '12345', email: 'short@test.com' }),
    });
    assert.equal(res.status, 400);
  });

  // S-004: Login with valid credentials -> 200 + JWT
  it('S-004: login with valid credentials returns 200 and JWT', async () => {
    await registerUser('charlie', 'mypassword', 'charlie@example.com');
    const res = await loginUser('charlie', 'mypassword');
    assert.equal(res.status, 200);
    assert.ok(res.body.token, 'should return a JWT');
    assert.equal(res.body.user.username, 'charlie');

    // Verify the token is a valid JWT
    const decoded = jwt.decode(res.body.token);
    assert.ok(decoded.id);
    assert.equal(decoded.username, 'charlie');
    assert.ok(decoded.exp, 'token should have expiry');
  });

  // S-005: Login with wrong password -> 401
  it('S-005: login with wrong password returns 401', async () => {
    await registerUser('dave', 'correctpass', 'dave@example.com');
    const res = await loginUser('dave', 'wrongpass');
    assert.equal(res.status, 401);
    assert.ok(res.body.error);
  });

  // S-006: Login with non-existent user -> 401
  it('S-006: login with non-existent user returns 401', async () => {
    const res = await loginUser('nonexistent_user', 'whatever');
    assert.equal(res.status, 401);
    assert.ok(res.body.error);
  });

  // S-013: Access protected route without token -> 401
  it('S-013: access cameras without token returns 401', async () => {
    const res = await fetch(`${base}/api/cameras`, {
      method: 'GET',
    });
    assert.equal(res.status, 401);
    const body = await res.json();
    assert.ok(body.error);
  });

  // S-014: Access protected route with expired token -> 401
  it('S-014: access cameras with expired token returns 401', async () => {
    // Create a token that expired 1 hour ago
    const config = require('../src/config');
    const expiredToken = jwt.sign(
      { id: 999, username: 'expired_user' },
      config.jwtSecret,
      { expiresIn: '-1h' }
    );

    const res = await fetch(`${base}/api/cameras`, {
      method: 'GET',
      headers: { Authorization: `Bearer ${expiredToken}` },
    });
    assert.equal(res.status, 401);
    const body = await res.json();
    assert.ok(body.error);
  });

  it('S-013: access cameras with malformed token returns 401', async () => {
    const res = await fetch(`${base}/api/cameras`, {
      method: 'GET',
      headers: { Authorization: 'Bearer not.a.valid.jwt' },
    });
    assert.equal(res.status, 401);
  });

  it('S-013: access cameras with missing Bearer prefix returns 401', async () => {
    const res = await fetch(`${base}/api/cameras`, {
      method: 'GET',
      headers: { Authorization: 'some-token-without-bearer' },
    });
    assert.equal(res.status, 401);
  });
});
