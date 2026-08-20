# Build from the repo root:
#   docker build -f examples/virt_room_server/Dockerfile .

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
    -S examples/virt_room_server \
    -B build/virt_room_server \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/virt_room_server -j$(nproc)

# Runtime stage
FROM alpine:3.20

RUN apk add --no-cache \
    libstdc++ \
    libgcc \
    mosquitto-libs

COPY --from=builder /src/build/virt_room_server/virt_room_server /usr/local/bin/virt_room_server

VOLUME ["/data"]

ENV NODE_NAME=VirtualRoomServer \
    MQTT_HOST=localhost \
    MQTT_PORT=1883 \
    MQTT_PACKET_TOPIC=meshcore/bridge/default/packets \
    MQTT_STATUS_TOPIC=meshcore/bridge/default/status

ENTRYPOINT ["/usr/local/bin/virt_room_server"]
