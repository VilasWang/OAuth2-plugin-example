# OAuth2 Admin Console

Management dashboard for the OAuth2 Authorization Server.

## Quick Start

```bash
# Install dependencies
npm install

# Start dev server (proxies API to localhost:5555)
npm run dev

# Build for production
npm run build
```

## Development

- Dev server: http://localhost:5174/admin/
- Backend must be running on port 5555
- Login with admin credentials (admin/admin in dev)

## Tech Stack

- Vue 3 + TypeScript
- Vite 5
- TailwindCSS v4
- Pinia (state management)
- Headless UI

## Pages

| Page | Route | Description |
|------|-------|-------------|
| Login | /admin/login | OAuth2 authentication |
| Dashboard | /admin/ | System health overview |
| Applications | /admin/applications | OAuth2 client management |
| Users | /admin/users | User management + role assignment |
| Audit Logs | /admin/logs | Security event viewer |
| Settings | /admin/settings | Scopes + system config |

## E2E Testing (Playwright)

```bash
# Install Playwright browsers (first time only)
npx playwright install chromium

# Run all E2E tests (headless)
npm run test:e2e

# Run with browser visible
npm run test:e2e:headed

# Run with Playwright UI (interactive debug)
npm run test:e2e:ui
```

Tests use API mocking (no backend required). Coverage:

| Test File | Tests | Scope |
|-----------|-------|-------|
| auth.spec.ts | 6 | Login / logout / MFA / permission denied |
| dashboard.spec.ts | 6 | Health status / quick links / error state |
| applications.spec.ts | 10 | CRUD / secret reset / empty state |
| users.spec.ts | 8 | List / role assignment / status badges |
| logs.spec.ts | 10 | Pagination / filters / empty state |
| settings.spec.ts | 8 | Scopes table / indicators |
| navigation.spec.ts | 5 | Sidebar / routing / active highlight |

Total: **53 tests**, runs in ~5 seconds.

## Docker Deployment

```bash
# Build and run with docker-compose (from project root)
docker-compose up -d oauth2-admin

# Access at http://localhost:8081/admin/
```

## API Endpoints Used

All endpoints require Bearer token with admin role:

- `POST /oauth2/login` — Authentication
- `POST /oauth2/token` — Token exchange
- `GET /oauth2/userinfo` — Current user info
- `GET /api/admin/clients` — List applications
- `POST /api/admin/clients` — Create application
- `DELETE /api/admin/clients/:id` — Delete application
- `POST /api/admin/clients/:id/reset-secret` — Reset secret
- `GET /api/admin/users` — List users
- `PUT /api/admin/users/:id/roles` — Assign roles
- `GET /api/admin/scopes` — List scopes
- `GET /api/admin/logs` — Audit logs
- `GET /health/ready` — System health
