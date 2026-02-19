const express = require('express');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const crypto = require('crypto');
const config = require('../config');
const User = require('../models/user');

const router = express.Router();

// POST /api/auth/register
router.post('/register', (req, res) => {
  const { username, password, email } = req.body;
  const normalizedUsername = String(username || '').trim();
  const normalizedEmail = String(email || '').trim().toLowerCase();

  if (!normalizedUsername || !password || !normalizedEmail) {
    return res.status(400).json({ error: 'username, password, and email are required' });
  }
  if (password.length < 6) {
    return res.status(400).json({ error: 'Password must be at least 6 characters' });
  }

  const existing = User.findByUsername(normalizedUsername);
  if (existing) {
    return res.status(409).json({ error: 'Username already taken' });
  }
  const existingEmail = User.findByEmail(normalizedEmail);
  if (existingEmail) {
    return res.status(409).json({ error: 'Email already registered' });
  }

  try {
    const passwordHash = bcrypt.hashSync(password, config.bcryptRounds);
    const userId = User.create(normalizedUsername, passwordHash, normalizedEmail);
    const token = jwt.sign({ id: userId, username: normalizedUsername }, config.jwtSecret, {
      expiresIn: config.jwtExpiresIn,
    });
    res.status(201).json({ token, user: { id: userId, username: normalizedUsername, email: normalizedEmail } });
  } catch (err) {
    if (err.message && err.message.includes('UNIQUE constraint failed')) {
      return res.status(409).json({ error: 'Username or email already taken' });
    }
    res.status(500).json({ error: 'Registration failed' });
  }
});

// POST /api/auth/login
router.post('/login', (req, res) => {
  const { username, password } = req.body;
  const identifier = String(username || '').trim();

  if (!identifier || !password) {
    return res.status(400).json({ error: 'username and password are required' });
  }

  const user = User.findByUsernameOrEmail(identifier);
  if (!user) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }

  const valid = bcrypt.compareSync(password, user.password_hash);
  if (!valid) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }

  const token = jwt.sign({ id: user.id, username: user.username }, config.jwtSecret, {
    expiresIn: config.jwtExpiresIn,
  });
  res.json({ token, user: { id: user.id, username: user.username, email: user.email } });
});

// POST /api/auth/forgot-password
router.post('/forgot-password', (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!email) {
    return res.status(400).json({ error: 'email is required' });
  }

  const user = User.findByEmail(email);
  if (!user) {
    return res.status(404).json({ error: 'Email not found' });
  }

  const temporaryPassword = crypto.randomBytes(6).toString('base64url');
  const passwordHash = bcrypt.hashSync(temporaryPassword, config.bcryptRounds);
  User.updatePasswordById(user.id, passwordHash);

  return res.json({
    message: 'Temporary password generated',
    temporaryPassword,
  });
});

module.exports = router;
