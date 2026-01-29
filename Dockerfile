# Build Stage
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# 1. Install System Dependencies (Drogon + OAuth2Backend Requirements)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3-pip \
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    libssl-dev \
    libbrotli-dev \
    libc-ares-dev \
    libpq-dev \
    libhiredis-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    pkg-config \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 2. Install Drogon (Source Build for v1.9.11)
WORKDIR /tmp/drogon
RUN git clone https://github.com/drogonframework/drogon.git . \
    && git checkout v1.9.11 \
    && git submodule update --init \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && make -j$(nproc) \
    && make install

# 3. Prepare Application
WORKDIR /app
COPY . .

# 4. Build Application using build.sh (Native Mode)
WORKDIR /app/OAuth2Backend
RUN sed -i 's/\r$//' build.sh && chmod +x build.sh && ./build.sh Release

# Runtime Stage
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime libs (Must match Build Stage)
RUN apt-get update && apt-get install -y \
    libjsoncpp25 \
    zlib1g \
    libssl3 \
    libbrotli1 \
    libc-ares2 \
    libpq5 \
    libhiredis0.14 \
    libsqlite3-0 \
    libcurl4 \
    libuuid1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy executable and config
COPY --from=builder /app/OAuth2Backend/build/OAuth2Server .
COPY --from=builder /app/OAuth2Backend/config.json ./config.json
# Copy web content
COPY --from=builder /app/OAuth2Backend/views ./views
# Create log and upload dirs
RUN mkdir -p logs uploads

EXPOSE 5555

CMD ["./OAuth2Server"]
