/**
 * Camera endpoint tests (S-007 to S-012)
 *
 * Uses Node.js built-in test runner (node:test) and native fetch.
 */

const { describe, it, before, after } = require('node:test');
const assert = require('node:assert/strict');

const { startServer, stopServer, getBaseUrl, getToken } = require('./setup');

describe('Cameras API', () => {
  let base;
  let userA; // primary test user
  let userB; // secondary test user (for ownership tests)

  before(async () => {
    base = await startServer();
    userA = await getToken('cam_user_a', 'password123', 'cam_a@test.com');
    userB = await getToken('cam_user_b', 'password123', 'cam_b@test.com');
  });

  after(async () => {
    await stopServer();
  });

  // Helper for authenticated requests
  function authFetch(url, token, options = {}) {
    return fetch(url, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${token}`,
        ...(options.headers || {}),
      },
    });
  }

  // S-007: Create camera with valid auth -> 201 + stream_key
  it('S-007: create camera returns 201 with stream_key', async () => {
    const res = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'Front Door', resolution: '1080p' }),
    });
    assert.equal(res.status, 201);

    const body = await res.json();
    assert.ok(body.id, 'should return camera id');
    assert.equal(body.name, 'Front Door');
    assert.ok(body.stream_key, 'should return a stream_key');
    assert.equal(body.resolution, '1080p');
  });

  it('S-007: create camera without name returns 400', async () => {
    const res = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'POST',
      body: JSON.stringify({ resolution: '720p' }),
    });
    assert.equal(res.status, 400);
  });

  // S-008: List cameras -> 200 + array
  it('S-008: list cameras returns 200 with array', async () => {
    // userA already has "Front Door" from S-007
    const res = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'GET',
    });
    assert.equal(res.status, 200);

    const body = await res.json();
    assert.ok(Array.isArray(body), 'should return an array');
    assert.ok(body.length >= 1, 'should have at least one camera');
    assert.equal(body[0].name, 'Front Door');
  });

  it('S-008: new user has empty camera list', async () => {
    const freshUser = await getToken();
    const res = await authFetch(`${base}/api/cameras`, freshUser.token, {
      method: 'GET',
    });
    assert.equal(res.status, 200);
    const body = await res.json();
    assert.ok(Array.isArray(body));
    assert.equal(body.length, 0);
  });

  // S-009: Update camera name -> 200
  it('S-009: update camera name returns 200', async () => {
    // Create a camera to update
    const createRes = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'Old Name' }),
    });
    const camera = await createRes.json();

    // Update the name
    const updateRes = await authFetch(`${base}/api/cameras/${camera.id}`, userA.token, {
      method: 'PUT',
      body: JSON.stringify({ name: 'New Name' }),
    });
    assert.equal(updateRes.status, 200);

    const updated = await updateRes.json();
    assert.equal(updated.name, 'New Name');
    assert.equal(updated.id, camera.id);
  });

  it('S-009: update non-existent camera returns 404', async () => {
    const res = await authFetch(`${base}/api/cameras/99999`, userA.token, {
      method: 'PUT',
      body: JSON.stringify({ name: 'Whatever' }),
    });
    assert.equal(res.status, 404);
  });

  // S-010: Delete own camera -> 200
  it('S-010: delete own camera returns 200', async () => {
    // Create a camera to delete
    const createRes = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'To Delete' }),
    });
    const camera = await createRes.json();

    const deleteRes = await authFetch(`${base}/api/cameras/${camera.id}`, userA.token, {
      method: 'DELETE',
    });
    assert.equal(deleteRes.status, 200);
    const body = await deleteRes.json();
    assert.ok(body.message);

    // Verify it is gone
    const listRes = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'GET',
    });
    const cameras = await listRes.json();
    const found = cameras.find((c) => c.id === camera.id);
    assert.equal(found, undefined, 'deleted camera should not appear in list');
  });

  // S-011: Delete another user's camera -> 403
  it('S-011: delete another user camera returns 403', async () => {
    // userB creates a camera
    const createRes = await authFetch(`${base}/api/cameras`, userB.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'UserB Camera' }),
    });
    const camera = await createRes.json();

    // userA tries to delete it
    const deleteRes = await authFetch(`${base}/api/cameras/${camera.id}`, userA.token, {
      method: 'DELETE',
    });
    assert.equal(deleteRes.status, 403);
  });

  it('S-011: update another user camera returns 403', async () => {
    // userB creates a camera
    const createRes = await authFetch(`${base}/api/cameras`, userB.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'UserB Camera 2' }),
    });
    const camera = await createRes.json();

    // userA tries to update it
    const updateRes = await authFetch(`${base}/api/cameras/${camera.id}`, userA.token, {
      method: 'PUT',
      body: JSON.stringify({ name: 'Hacked Name' }),
    });
    assert.equal(updateRes.status, 403);
  });

  // S-012: Get stream info -> 200 + signaling details
  it('S-012: get stream info returns 200 with signaling details', async () => {
    // Create a camera
    const createRes = await authFetch(`${base}/api/cameras`, userA.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'Stream Camera' }),
    });
    const camera = await createRes.json();

    // Get stream info
    const streamRes = await authFetch(`${base}/api/cameras/${camera.id}/stream`, userA.token, {
      method: 'GET',
    });
    assert.equal(streamRes.status, 200);

    const body = await streamRes.json();
    assert.ok(body.stream_key, 'should include stream_key');
    assert.ok(body.signaling_url, 'should include signaling_url');
    assert.ok(body.room, 'should include room');
    assert.ok(body.status !== undefined, 'should include status');
  });

  it('S-012: get stream info for non-existent camera returns 404', async () => {
    const res = await authFetch(`${base}/api/cameras/99999/stream`, userA.token, {
      method: 'GET',
    });
    assert.equal(res.status, 404);
  });

  it('S-012: get stream info for another user camera returns 403', async () => {
    // userB creates a camera
    const createRes = await authFetch(`${base}/api/cameras`, userB.token, {
      method: 'POST',
      body: JSON.stringify({ name: 'Private Camera' }),
    });
    const camera = await createRes.json();

    // userA tries to get stream info
    const res = await authFetch(`${base}/api/cameras/${camera.id}/stream`, userA.token, {
      method: 'GET',
    });
    assert.equal(res.status, 403);
  });
});
