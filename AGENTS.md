# AGENTS.md - RealLive Development Guide

This document provides essential information for agents working on the RealLive codebase.

## Project Overview

RealLive is a real-time video surveillance system consisting of:
- **Server** (Node.js/Express + SQLite) - API and WebSocket signaling
- **Web** (Vue 3 + Vite) - Frontend UI
- **Pusher** (C++) - Camera capture and RTMP streaming
- **Puller** (C++) - Stream recording to MP4
- **SRS** - Media server (Docker)

## Build, Lint, and Test Commands

### Server (Node.js Backend)

```bash
cd server

# Install dependencies
npm install

# Development (auto-restart on file changes)
npm run dev

# Production start
npm run start

# Run all tests
npm test

# Run a single test file
node --test test/cameras.test.js
node --test test/auth.test.js
```

### Web (Vue 3 Frontend)

```bash
cd server/web

# Install dependencies
npm install

# Development server (hot reload)
npm run dev

# Production build
npm run build

# Preview production build
npm run preview
```

### Running Tests

The server uses Node.js built-in test runner (`node:test`). Tests are located in `server/test/`:

- `cameras.test.js` - Camera CRUD and stream API tests
- `auth.test.js` - Authentication tests
- `setup.js` - Test helpers (temporary DB, server startup)

To run a single test with verbose output:
```bash
node --test --verbose test/cameras.test.js
```

## Code Style Guidelines

### Backend (Node.js/Express)

**Language**: JavaScript (CommonJS)
**Framework**: Express.js
**Database**: SQLite with better-sqlite3

#### Imports
- Use CommonJS `require()` for all imports
- Order: built-in modules → external packages → local modules
- Example:
```javascript
const express = require('express');
const { v4: uuidv4 } = require('uuid');
const authMiddleware = require('../middleware/auth');
const Camera = require('../models/camera');
```

#### Error Handling
- Return proper HTTP status codes with JSON error objects
- Validate input and return 400 for bad requests
- Return 401 for authentication failures, 403 for authorization failures
- Return 404 for not found
- Example:
```javascript
if (!camera) {
  return res.status(404).json({ error: 'Camera not found' });
}
if (camera.user_id !== req.user.id) {
  return res.status(403).json({ error: 'Forbidden' });
}
```

#### Routing
- All routes in `server/src/routes/`
- Use RESTful patterns: GET, POST, PUT, DELETE
- All camera routes require authentication middleware
- Parameter validation before database operations

#### Models
- Factory pattern in `server/src/models/`
- Use prepared statements for SQL queries (better-sqlite3)
- Always include ownership checks (user_id)

#### Logging
- Use `console.log()` with `[ModuleName]` prefix
- Example: `console.log('[Camera API] Getting stream info for camera', camera.id)`

### Frontend (Vue 3)

**Language**: JavaScript (ES Modules via Vite)
**Framework**: Vue 3 with Composition API
**State Management**: Pinia
**Routing**: Vue Router

#### Component Structure
```vue
<script setup>
import { ref, computed } from 'vue'

const props = defineProps({
  camera: { type: Object, required: true },
})

const emit = defineEmits(['edit', 'delete', 'watch'])
</script>

<template>
  <!-- Template content -->
</template>

<style scoped>
/* Scoped styles */
</style>
```

#### Composition API
- Use `<script setup>` syntax
- Use `defineProps()` and `defineEmits()` for props/events
- Use `ref()` for reactive primitives, `reactive()` for objects
- Use `computed()` for derived state

#### Stores (Pinia)
- Located in `server/web/src/stores/`
- Use `defineStore()` with arrow function
- Example:
```javascript
import { defineStore } from 'pinia'

export const useAuthStore = defineStore('auth', () => {
  const token = ref(null)
  // ...
  return { token }
})
```

#### API Calls
- Use native `fetch()` for API requests
- Include Authorization header with Bearer token
- Handle errors with try/catch

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Files (JS) | kebab-case | `auth-middleware.js` |
| Files (Vue) | PascalCase | `CameraCard.vue` |
| Directories | kebab-case | `server/src/routes/` |
| Variables | camelCase | `cameraList`, `isActive` |
| Constants | UPPER_SNAKE_CASE | `MAX_CAMERAS`, `JWT_SECRET` |
| Classes/Models | PascalCase | `Camera`, `User` |
| Database tables | snake_case | `cameras`, `users` |
| Vue components | PascalCase | `<CameraCard />`, `<Navbar />` |

### General Guidelines

1. **No comments unless requested** - Write self-documenting code
2. **No linting tools configured** - Code is unopinionated about formatting
3. **Security** - Never expose secrets, use environment variables for sensitive data
4. **Database queries** - Use parameterized queries to prevent SQL injection
5. **Authentication** - Use JWT tokens, verify on every protected route

## Project Structure

```
reallive/
├── server/
│   ├── src/
│   │   ├── app.js           # Express app setup
│   │   ├── server.js        # Server entry point
│   │   ├── config.js        # Configuration
│   │   ├── routes/         # API routes
│   │   ├── models/         # Database models
│   │   ├── middleware/     # Express middleware
│   │   ├── services/       # Business logic
│   │   └── signaling/     # WebSocket signaling
│   ├── test/               # Test files
│   └── web/                # Vue frontend
│       ├── src/
│       │   ├── components/
│       │   ├── views/
│       │   ├── stores/
│       │   └── App.vue
│       └── vite.config.js
├── pusher/                 # C++ stream pusher
├── puller/                 # C++ stream puller
└── docs/                   # Documentation
```

## Common Development Tasks

### Adding a new API endpoint
1. Create route file in `server/src/routes/`
2. Add model methods in `server/src/models/`
3. Import and mount in `server/src/app.js`
4. Add tests in `server/test/`

### Adding a new Vue component
1. Create `.vue` file in appropriate directory
2. Use Composition API with `<script setup>`
3. Add to parent component or view

### Running full stack
```bash
# Terminal 1: Start SRS (Docker)
docker run -d --name srs -p 1935:1935 -p 80:80 ossrs/srs:5

# Terminal 2: Start server
cd server && npm run dev

# Terminal 3: Start web dev server
cd server/web && npm run dev
```
