# ==========================================
# Unified Dockerfile for authforge
# Supports multiple targets: backend-dev, backend-runtime, frontend-runtime
# ==========================================

# Global Arguments for customization
ARG DROGON_REPO=https://github.com/drogonframework/drogon.git
ARG DROGON_VERSION=v1.9.13
ARG NODE_VERSION=22-alpine

# --- Stage 1: Backend Build Environment (Base) ---
FROM ubuntu:22.04 AS backend-base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git python3-pip libjsoncpp-dev uuid-dev zlib1g-dev \
    libssl-dev libbrotli-dev libc-ares-dev libpq-dev libhiredis-dev \
    libsqlite3-dev libcurl4-openssl-dev pkg-config wget ca-certificates \
    postgresql-client redis-tools iputils-ping netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Install Drogon
ARG DROGON_REPO
ARG DROGON_VERSION
WORKDIR /tmp/drogon
RUN git clone ${DROGON_REPO} . && \
    git checkout ${DROGON_VERSION} && \
    git submodule update --init --recursive && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && \
    make -j$(nproc) && make install && \
    ldconfig && rm -rf /tmp/drogon

# --- Stage 2: Backend Development (Target: backend-dev) ---
FROM backend-base AS backend-dev
WORKDIR /app
CMD ["/bin/bash"]

# --- Stage 3: Backend Application Builder ---
FROM backend-base AS backend-builder
WORKDIR /app
COPY . .
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON && \
    make -j$(nproc)

# --- Stage 4: Backend Runtime (Target: backend-runtime) ---
FROM ubuntu:22.04 AS backend-runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    libjsoncpp25 zlib1g libssl3 libbrotli1 libc-ares2 libpq5 \
    libhiredis0.14 libsqlite3-0 libcurl4 libuuid1 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
# Phase 5 (directory restructure): sources live in apps/server, the binary
# is authforge-server; the in-container layout (/app/config.json, /app/views,
# /app/sql/{migrations,seed}) is kept unchanged.
COPY --from=backend-builder /app/build/apps/server/authforge-server .
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
