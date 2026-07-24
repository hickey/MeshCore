# Build from the repo root:
#   docker build -f examples/virt_companion/Dockerfile .

# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    git \
    libmosquitto-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake \
    -S examples/virt_companion \
    -B build/virt_companion \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/virt_companion -j$(nproc)

# Runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libmosquitto1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/virt_companion/virt_companion /usr/local/bin/virt_companion

VOLUME ["/data"]
EXPOSE 5000

ENV TCP_PORT=5000 \
    NODE_NAME=VirtualCompanion \
    MQTT_HOST=localhost \
    MQTT_PORT=1883 \
    MQTT_TOPIC=meshcore/packets

ENTRYPOINT ["/usr/local/bin/virt_companion"]
