const path = require('path');

module.exports = {
  port: process.env.PORT || 3000,
  jwtSecret: process.env.JWT_SECRET || 'reallive-dev-secret-change-in-production',
  jwtExpiresIn: '24h',
  bcryptRounds: 12,
  dbPath: process.env.DB_PATH || path.join(__dirname, '..', 'data', 'reallive.db'),
};
