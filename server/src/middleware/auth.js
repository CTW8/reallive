const AuthSession = require('../models/auth-session');
const { verifyToken } = require('../services/auth-token-service');

function authMiddleware(req, res, next) {
  const header = req.headers.authorization;
  if (!header || !header.startsWith('Bearer ')) {
    return res.status(401).json({ error: 'No token provided' });
  }

  const token = header.slice(7);
  try {
    const payload = verifyToken(token);
    if (payload?.typ !== 'access' || !payload?.sid || !payload?.id) {
      return res.status(401).json({ error: 'Invalid token type' });
    }
    const session = AuthSession.findById(Number(payload.sid));
    if (!AuthSession.isActive(session)) {
      return res.status(401).json({ error: 'Session expired' });
    }
    req.user = { id: payload.id, username: payload.username, sessionId: Number(payload.sid) };
    AuthSession.touch(Number(payload.sid), req.ip, req.headers['user-agent'] || '');
    next();
  } catch (err) {
    return res.status(401).json({ error: 'Invalid or expired token' });
  }
}

module.exports = authMiddleware;
