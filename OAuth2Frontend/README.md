# OAuth2 Vue Frontend

A modern Vue 3 + Vite application demonstrating OAuth2.0 Client implementation.

## Features

- **OAuth2 Login**: Standard Authorization Code Flow integration.
- **Multi-Provider**: Support for Login with Google and WeChat (Simulated/Integrated).
- **Responsive UI**: Beautiful, glassmorphism-inspired design.
- **Testing**: Integrated Unit Tests with Vitest.

## Project Setup

### Prerequisites

- Node.js (v16+)
- npm

### Installation

```bash
npm install
```

### Development

Start the development server:

```bash
npm run dev
```

App runs at `http://localhost:5173`.

## Testing

Run unit tests with Vitest:

```bash
npm test
```

## Configuration

Configuration is currently hardcoded in `src/views/Login.vue` and `Callback.vue` for demo purposes.

- **Client ID**: `vue-client`
- **Redirect URI**: `http://localhost:5173/callback`
- **Backend URL**: `http://localhost:5555`

## Documentation

- [Authorization Flow Guide](docs/FRONTEND_AUTH_FLOW.md)

## Tech Stack

- **Framework**: Vue 3 (Composition API)
- **Build Tool**: Vite
- **Routing**: Vue Router
- **Testing**: Vitest + Vue Test Utils
- **Styling**: Native CSS (Scoped)
