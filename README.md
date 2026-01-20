# Drogon OAuth2.0 Provider & Vue Client Demo

This project demonstrates how to implement a fully functional OAuth2.0 Provider (Server) using the [Drogon C++ Web Framework](https://github.com/drogonframework/drogon) and a modern Client Application using [Vue.js](https://vuejs.org/).

It implements the **Authorization Code Grant** flow and supports:

1. **Local Authentication**: Login with credentials stored in the Drogon backend.
2. **External Provider Integration**: A "Login with WeChat" flow demonstrating server-side integration with the WeChat Open Platform API.

## Project Structure

```
OAuth2Test/
├── OAuth2Backend/      # C++ OAuth2 Provider (Drogon)
│   ├── controllers/    # API verify (OAuth2, WeChat)
│   ├── filters/        # Middleware (Token Validation)
│   ├── plugins/        # Core OAuth2 Logic Plugin
│   ├── views/          # Server-side Login Pages (CSP)
│   └── config.json     # App Configuration
└── OAuth2Frontend/     # Vue.js Client Application
    ├── src/views/      # Login & Callback Pages
    └── ...
```

## Prerequisites

- **Backend**:
  - C++ Compiler (supporting C++17/20, e.g., MSVC, GCC)
  - [Conan](https://conan.io/) (Package Manager)
  - [CMake](https://cmake.org/)
- **Frontend**:
  - [Node.js](https://nodejs.org/) & npm

## 1. Backend Setup (OAuth2Backend)

 The backend handles OAuth2 requests, issues tokens, and validates API access.

### dependency Installation & Build

```powershell
cd OAuth2Backend
# Install dependencies and configure CMake
./build.bat
```

*(The `build.bat` script runs `conan install` and `cmake`, then builds the project.)*

### Running the Server

```powershell
cd OAuth2Backend/build
Release/OAuth2Server.exe
```

The server listens on `http://localhost:5555`.

### Configuration (Optional: WeChat)

To enable real WeChat login, edit `controllers/WeChatController.cc` and replace `YOUR_WECHAT_APPID` / `YOUR_WECHAT_SECRET` with your actual credentials.

## 2. Frontend Setup (OAuth2Frontend)

The frontend is a Single Page Application (SPA) acting as the OAuth2 Client.

### Installation

```bash
cd OAuth2Frontend
npm install
```

### Running the Client

```bash
npm run dev
```

The client runs on `http://localhost:5173`.

### Configuration (Optional: WeChat)

To enable real WeChat login, edit `src/views/Login.vue` and set `APPID` and `REDIRECT_URI` (Must match your domain).

## Storage & Persistence

The project supports pluggable storage backends for OAuth2 data.

### Supported Backends

1. **Memory** (Default): Fast, volatile storage. Best for testing.
2. **PostgreSQL**: Persistent, SQL-based storage.
3. **Redis**: High-performance, persistent Key-Value storage.

### Configuration

Edit `OAuth2Backend/config.json`:

```json
{
  "oauth2": {
    "storage_type": "redis" // Options: "memory", "postgres", "redis"
  },
  "redis": {
    "host": "127.0.0.1",
    "port": 6379,
    "passwd": "your_password"
  }
}
```

### Security Hardening

Client Secrets are securely stored using **SHA256 Hashing with Salt**.

### Persistence & Storage

This project uses a flexible persistence layer supporting **PostgreSQL** (Production) and **Redis** (High Performance).

For detailed architecture, supported backends, and schema designs, please refer to:
👉 **[Data Persistence Guide](OAuth2Backend/docs/data_persistence.md)**

### Data Consistency & Security

We implement **Atomic Consume** operations and **SHA256 Hashing** to ensure high security and consistency.

For implementation details (Lua Scripts, Threat Models, Token Lifecycle):
👉 **[Data Consistency Guide](OAuth2Backend/docs/data_consistency.md)**
👉 **[Security Architecture](OAuth2Backend/docs/security_architecture.md)**

### Observability

Production-ready monitoring with Prometheus Metrics and Structured Audit Logs.
👉 **[Observability Guide](OAuth2Backend/docs/observability.md)**

### Security Hardening (New)

We implement Rate Limiting and Security Headers to protect against attacks.
👉 **[Security Hardening Guide](OAuth2Backend/docs/security_hardening.md)**

## Features & Endpoints

> **OpenAPI Specification**: [openapi.yaml](OAuth2Backend/openapi.yaml)

| Feature | Endpoint / Description |
|---------|------------------------|
| **Authorize** | `GET /oauth2/authorize` - Logic to handle Authorization requests. |
| **Token** | `POST /oauth2/token` - Exchange Auth Code for Access Token. |
| **User Info** | `GET /oauth2/userinfo` - Protected Endpoint (Requires Bearer Token). |
| **WeChat Login** | `POST /api/wechat/login` - Server-side exchange of WeChat code for Session. |
| **Persistence** | Support for Redis/Postgres backends via Strategy Pattern. |

## Usage Guide

1. **Start Backend & Frontend** using the commands above.
2. Open **<http://localhost:5173>**.
3. **Local Login**:
    - Click "Login with Drogon".
    - Credentials: `admin` / `admin`.
    - Observe successful redirect and user info display.
4. **WeChat Login**:
    - Requires valid AppID Configuration.
    - Click "Login with WeChat", scan QR code, and verify login.
