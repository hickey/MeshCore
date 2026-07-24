# Build from the repo root:
#   docker build -f examples/virt_companion/Dockerfile .

# Build stage
FROM alpine:3.20 AS builder

RUN apk add --no-cache \
    cmake \
    make \
    g++ \
    git \
    mosquitto-dev

WORKDIR /src
COPY . .

RUN cmake \
    -S examples/virt_companion \
    -B build/virt_companion \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/virt_companion -j$(nproc)

# Runtime stage
FROM alpine:3.20

RUN apk add --no-cache \
    libstdc++ \
    libgcc \
    mosquitto-libs

COPY --from=builder /src/build/virt_companion/virt_companion /usr/local/bin/virt_companion

VOLUME ["/data"]
EXPOSE 5000

ENV TCP_PORT=5000 \
    NODE_NAME=VirtualCompanion \
    MQTT_HOST=localhost \
    MQTT_PORT=1883 \
    MQTT_TOPIC=meshcore/packets

ENTRYPOINT ["/usr/local/bin/virt_companion"]
