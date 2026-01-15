# Drogon OAuth2.0 Provider & Vue Client Demo

This project demonstrates how to implement a fully functional OAuth2.0 Provider (Server) using the [Drogon C++ Web Framework](https://github.com/drogonframework/drogon) and a modern Client Application using [Vue.js](https://vuejs.org/).

It implements the **Authorization Code Grant** flow and supports:
1.  **Local Authentication**: Login with credentials stored in the Drogon backend.
2.  **External Provider Integration**: A "Login with WeChat" flow demonstrating server-side integration with the WeChat Open Platform API.

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

## Features & Endpoints

| Feature | Endpoint / Description |
|---------|------------------------|
| **Authorize** | `GET /oauth2/authorize` - Logic to handle Authorization requests. |
| **Token** | `POST /oauth2/token` - Exchange Auth Code for Access Token. |
| **User Info** | `GET /oauth2/userinfo` - Protected Endpoint (Requires Bearer Token). |
| **WeChat Login** | `POST /api/wechat/login` - Server-side exchange of WeChat code for Session. |
| **CORS** | Global CORS enabled for smooth Frontend-Backend communication. |

## Usage Guide

1.  **Start Backend & Frontend** using the commands above.
2.  Open **http://localhost:5173**.
3.  **Local Login**:
    *   Click "Login with Drogon".
    *   Credentials: `admin` / `admin`.
    *   Observe successful redirect and user info display.
4.  **WeChat Login**:
    *   Requires valid AppID Configuration.
    *   Click "Login with WeChat", scan QR code, and verify login.
