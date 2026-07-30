# ==========================================
# Unified Dockerfile for authforge
# Supports multiple targets: backend-dev, backend-runtime, frontend-runtime
# ==========================================

# Global Arguments for customization
ARG NODE_VERSION=22-alpine

# --- Stage 1: Backend Build Environment (Base) ---
# C/C++ libraries (Drogon, jsoncpp, libpq, hiredis, ...) come from Conan, not
# apt — parity with scripts/backend/build.sh and CI (_build-test.yml). apt only
# bootstraps the compiler toolchain plus the client tools used by backend-dev.
FROM ubuntu:22.04 AS backend-base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config python3-pip wget ca-certificates \
    libssl-dev uuid-dev zlib1g-dev \
    postgresql-client redis-tools iputils-ping netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Conan 2.x resolves all C/C++ dependencies (drogon/1.9.13 per conanfile.py).
# cmake also comes from pip: Ubuntu 22.04's apt cmake (3.22) cannot read the
# Conan-generated CMakeUserPresets.json (schema version 4 needs CMake >= 3.23);
# /usr/local/bin precedes /usr/bin so the pip cmake wins.
RUN pip3 install --no-cache-dir "conan>=2,<3" "cmake>=3.23" && \
    conan profile detect --force

# --- Stage 2: Backend Development (Target: backend-dev) ---
FROM backend-base AS backend-dev
WORKDIR /app
CMD ["/bin/bash"]

# --- Stage 3: Backend Application Builder ---
# Reuse the canonical build script (Conan install + `cmake --preset
# linux-release` + drogon_ctl PATH wiring) instead of a hand-rolled cmake
# invocation, so the container build cannot drift from host/CI builds.
# The BuildKit cache mount keeps the Conan cache (downloaded sources + built
# binaries, e.g. the boost/drogon source builds) across image rebuilds, so a
# transient network failure does not restart hours of `--build=missing` work.
# The mount shadows the image's /root/.conan2, so re-detect the profile here.
FROM backend-base AS backend-builder
WORKDIR /app
COPY . .
RUN --mount=type=cache,target=/root/.conan2 \
    conan profile detect --force && \
    bash scripts/backend/build.sh Release

# --- Stage 4: Backend Runtime (Target: backend-runtime) ---
# Conan links all C/C++ dependencies statically: `ldd authforge-server` inside
# the image resolves only libstdc++/libm/libgcc_s/libc, which the ubuntu:22.04
# base already ships. The former per-library apt list (libjsoncpp25, libpq5,
# libhiredis0.14, ...) is gone; only TLS roots for outbound HTTPS (SMTP,
# social login) and curl for container healthchecks remain.
FROM ubuntu:22.04 AS backend-runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
# Phase 5 (directory restructure): sources live in apps/server, the binary
# is authforge-server; the in-container layout (/app/config.json, /app/views,
# /app/sql/{migrations,seed}) is kept unchanged. The binary now comes from the
# Conan preset build dir (build/linux-release, see CMakePresets.json binaryDir).
COPY --from=backend-builder /app/build/linux-release/apps/server/authforge-server .
COPY --from=backend-builder /app/apps/server/config/config.prod.json ./config.json
COPY --from=backend-builder /app/apps/server/views ./views
COPY --from=backend-builder /app/apps/server/migrations ./sql/migrations
COPY --from=backend-builder /app/apps/server/seed ./sql/seed
RUN mkdir -p logs uploads
EXPOSE 5555
CMD ["./authforge-server"]

# --- Stage 5: Frontend Builder ---
# Only proxy Node as it was the one failing
FROM node:${NODE_VERSION} AS frontend-builder
# Vite build-time vars (inlined into the SPA bundle via import.meta.env.VITE_*).
# Passed through from docker-compose.prod.yml build.args; default empty so the
# SPA falls back to its in-code defaults when a var is not provided.
ARG VITE_API_BASE_URL=
ARG VITE_CLIENT_ID=vue-client
ARG VITE_REDIRECT_URI=
ARG VITE_GITHUB_CLIENT_ID=
ENV VITE_API_BASE_URL=$VITE_API_BASE_URL \
    VITE_CLIENT_ID=$VITE_CLIENT_ID \
    VITE_REDIRECT_URI=$VITE_REDIRECT_URI \
    VITE_GITHUB_CLIENT_ID=$VITE_GITHUB_CLIENT_ID
WORKDIR /app/frontends/user
COPY frontends/user/package*.json ./
RUN npm install
COPY frontends/user/ .
RUN npm run build

# --- Stage 6: Frontend Runtime (Target: frontend-runtime) ---
FROM nginx:stable-alpine AS frontend-runtime
COPY --from=frontend-builder /app/frontends/user/dist /usr/share/nginx/html
COPY frontends/user/nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
