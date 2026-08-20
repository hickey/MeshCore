# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

MeshCore uses [PlatformIO](https://platformio.org/) for embedded firmware builds. There are no unit tests — CI verifies correctness by confirming the firmware compiles for target platforms.

**List available targets:**
```sh
sh build.sh list
```

**Build a specific firmware variant:**
```sh
pio run -e <environment>          # e.g. pio run -e heltec_v3
sh build.sh build-firmware <target>
```

**Build all variants by type:**
```sh
sh build.sh build-companion-firmwares
sh build.sh build-repeater-firmwares
sh build.sh build-room-server-firmwares
```

Local PlatformIO overrides go in `platformio.local.ini` (gitignored, auto-loaded).

## Code Style

Formatting is defined in `.clang-format`:
- 2-space indentation, no tabs
- 110-character line limit
- `camelCase` for functions/variables, `PascalCase` for classes, `ALL_CAPS` for `#define` constants
- Opening brace on same line

Don't reformat existing code wholesale — keep diffs clean.

## Architecture

MeshCore is a portable C++ library for multi-hop LoRa mesh networking on embedded devices (ESP32, nRF52, RP2040, STM32). The library is consumed by example applications, each targeting a specific use case.

### Core Library (`src/`)

| File | Role |
|------|------|
| `Mesh.h/.cpp` | Main mesh engine — packet routing, neighbor tracking, hop logic |
| `Dispatcher.h/.cpp` | Application-layer message dispatch and handler registration |
| `Packet.h/.cpp` | Wire format, serialization, and encryption |
| `Identity.h/.cpp` | Per-device identity and Ed25519 key management |
| `MeshCore.h` | Global constants, protocol limits, and shared abstractions |

Packets are max 184 bytes. The protocol uses path hashing, hop limits, and optional encryption/signatures. Zero-hop packets are used for neighbor discovery.

### Helpers (`src/helpers/`)

Reusable building blocks consumed by examples:
- `BaseChatMesh` — base class for chat-capable mesh nodes
- `CommonCLI` — full CLI framework used across companion/repeater/server apps
- `bridges/` — BLE, USB Serial, WiFi bridges for companion radio connectivity
- `radiolib/` — wrappers around RadioLib for specific radio modules
- `ui/` — button and menu abstractions for devices with displays

### Example Applications (`examples/`)

Each example is a complete firmware targeting a role:
- `companion_radio` — user-facing app with BLE/USB/WiFi; connects to mobile apps
- `simple_repeater` — network extender with ACL
- `simple_room_server` — BBS-style chat server
- `kiss_modem` — KISS protocol serial bridge for external software
- `simple_secure_chat` — terminal-based encrypted chat
- `simple_sensor` — remote telemetry node

### Architecture-Specific Code (`arch/`)

Platform HAL split by MCU family: `esp32/`, `nrf52/`, `stm32/`. Board pin mappings live in `boards/`; per-variant PlatformIO configs live in `variants/` (74 variants).

### Key Dependencies

- `RadioLib ^7.6.0` — LoRa radio abstraction (built with `RADIOLIB_STATIC_ONLY=1`, `RADIOLIB_GODMODE=1`)
- `rweather/Crypto ^0.4.0` — cryptographic primitives
- `lib/ed25519/` — local Ed25519 implementation for signing
- `lib/nrf52/` — local Nordic BLE stack

## Packet Logging (`MESH_PACKET_LOGGING`)

Add `-DMESH_PACKET_LOGGING=1` to `build_flags` in `platformio.local.ini` to enable real-time packet tracing to `Serial`. Not enabled in any shipped environment — local debug only.

**RX line** (`Dispatcher.cpp:219`) — emitted after a packet passes validation:
```
<datetime>: RX, len=<N> (type=<T>, route=<D|F>, payload_len=<N>) SNR=<N> RSSI=<N> score=<N> time=<N> hash=<hex> [<src> -> <dst>]
```

**TX line** (`Dispatcher.cpp:340`) — emitted just before the radio send:
```
<datetime>: TX, len=<N> (type=<T>, route=<D|F>, payload_len=<N>) [<src> -> <dst>]
```

- `route`: `D` = direct, `F` = flood
- `[src -> dst]`: first two payload bytes, only printed for PATH, REQ, RESPONSE, and TXT_MSG types
- `hash`: full packet hash (RX only)

In `simple_repeater` and `simple_room_server`, a raw hex dump of the wire bytes is also printed before the decoded RX line:
```
<datetime> RAW: <hex bytes>
```

## Bridge Packet Formats

Both bridges share magic `0xC03E` and Fletcher-16 checksums but have different frame layouts.

**RS232Bridge** (`WITH_RS232_BRIDGE`) — `src/helpers/bridges/RS232Bridge.h`:
```
[2 bytes] Magic header: 0xC03E
[2 bytes] Payload length
[N bytes] Raw mesh packet bytes
[2 bytes] Fletcher-16 checksum (over payload only)
```
Fixed baud rate of 115200. Total framing overhead is 6 bytes. RX/TX pins are set via `WITH_RS232_BRIDGE_RX` / `WITH_RS232_BRIDGE_TX` build defines.

**ESPNowBridge** (`WITH_ESPNOW_BRIDGE`) — `src/helpers/bridges/ESPNowBridge.h`:
```
[2 bytes] Magic header: 0xC03E
[2 bytes] Fletcher-16 checksum (over the *encrypted* payload)
[≤246 bytes] XOR-encrypted mesh packet bytes
```
No explicit length field — length comes from the ESP-NOW receive callback. The payload is XOR-encrypted using `_prefs->bridge_secret` as the repeating key *before* transmission; checksum is computed after encryption. Nodes with a different secret fail checksum validation and discard the packet, providing network isolation. Broadcast-based: all peers receive all packets.

**USBSerialBridge** (`WITH_USB_SERIAL_BRIDGE`) — `src/helpers/bridges/USBSerialBridge.h`:

Same frame format as RS232Bridge. Takes any `Stream` (e.g. `Serial` on ESP32-S3 native USB). No pin or baud configuration — the USB CDC port is already initialised by the framework.

**TCPBridge** (`WITH_TCP_BRIDGE`) — `src/helpers/bridges/TCPBridge.h`:

Same frame format as RS232Bridge, carried over a WiFi TCP socket. Listens on the port set by the define; accepts one client at a time (new connection displaces the old one). WiFi must be connected before `bridge.begin()` is called.

**MQTTBridge** (`WITH_MQTT_BRIDGE`) — `src/helpers/bridges/MQTTBridge.h/.cpp`:

Runs over WiFi + MQTT instead of a wired/RF sideband. Packets are hex-encoded as uppercase ASCII (no magic header, length field, or checksum — MQTT's own transport handles integrity) and published/subscribed on a single topic (`MQTT_PACKET_TOPIC`). Uses `PubSubClient` and requires `WiFi.h`; WiFi must be started (via `WIFI_SSID`/`WIFI_PWD` build defines, or manually before `bridge.begin()`) since the bridge only attempts to connect to the broker once WiFi is associated, and retries every 5s in `loop()` if disconnected. The MQTT client ID is derived from the node's own pubkey prefix (`meshcore-mqtt-bridge-<hex6>`). Like the other bridges, `SimpleMeshTables` (`_seen_packets`) suppresses re-publishing/re-injecting packets already seen, which also keeps a node from re-processing its own publish via its own subscription.

Configuration build defines: `MQTT_HOST` (required), `MQTT_PORT` (default 1883), `MQTT_USERNAME`/`MQTT_PASSWORD` (optional), `MQTT_PACKET_TOPIC` (default `meshcore/bridge/default/packets`), `MQTT_STATUS_TOPIC` (default `meshcore/bridge/default/status`), `MQTT_STATUS_INTERVAL` (default 60 seconds), `MQTT_DEBUG`. `mqtt.host`/`mqtt.port`/`mqtt.packet.topic`/`mqtt.status.topic` can also be changed at runtime via CLI (`set mqtt.host <host>`, etc.) — each change tears down and reinitializes the MQTT client (`end()` → `initialize()` → `reconnect()`).

When connecting to the MQTT broker, the bridge publishes a status message to `MQTT_STATUS_TOPIC/<pubkey_hex>` (e.g., `meshcore/bridge/default/status/A1B2C3D4...`) with the retain flag set. The status is then republished periodically at the interval defined by `MQTT_STATUS_INTERVAL` (default 60 seconds). An MQTT Last Will and Testament (LWT) is also configured to publish `{"status":"offline"}` to the same topic when the client disconnects unexpectedly.

The online status message includes comprehensive statistics:
- `status`: "online" (when bridge_source is non-zero) or "not-bridging" (when bridge_source is 0)
- `bridge_source`: "rx", "tx", "both", or "none"
- `uptime_secs`: Node uptime in seconds
- `tx_air_secs`, `rx_air_secs`: Total transmit and receive airtime in seconds
- `tx_packets`, `rx_packets`: Total packets sent and received
- `tx_flood`, `tx_direct`: Flood and direct packets sent
- `rx_flood`, `rx_direct`: Flood and direct packets received
- `dup_flood`, `dup_direct`: Duplicate flood and direct packets detected
- `noise_floor`: Current noise floor (dBm)
- `last_rssi`: Last received signal strength (dBm)
- `last_snr`: Last signal-to-noise ratio
- `rx_errors`: Received packet errors
- `tx_queue_len`: Current transmit queue length
- `clock`: Unix timestamp from RTC

#### Zero-hop advert re-transmission

Zero-hop adverts (`PAYLOAD_TYPE_ADVERT`, `ROUTE_TYPE_DIRECT`, `path_len == 0`) received over MQTT need special handling: the normal receive path (`BridgeBase::handleReceivedPacket()` → `_mgr->queueInbound()` → `Mesh::onRecvPacket()` → `routeRecvPacket()`) only retransmits **flood**-route packets, so a zero-hop advert queued that way is processed locally (neighbor table updated) but never actually goes out over radio.

To fix this, `MQTTBridge` holds a `mesh::Mesh* _mesh` pointer (set via `setMesh()`, called from `MyMesh`'s constructor in `examples/simple_repeater/MyMesh.cpp`). In `onPacketReceived()`, any received advert that is zero-hop and not our own pubkey is passed directly to `_mesh->sendZeroHop(packet)` instead of being queued — this transmits it over LoRa as a genuine zero-hop packet (`ROUTE_TYPE_DIRECT`, `path_len = 0`), so only radio neighbors of this node receive it, and other repeaters do **not** flood-retransmit it further. Duplicate suppression still goes through `_seen_packets.wasSeen()`/`markSeen()` before sending. All other packet types continue through the normal `handleReceivedPacket()` path.

All bridges use `SimpleMeshTables` for duplicate detection to prevent re-forwarding packets already seen.

### Bridge packet source (`bridge_pkt_src`, `set bridge.source`)

`bridge_pkt_src` in `NodePrefs` (`src/helpers/CommonCLI.h`) is a bitmask (`BRIDGE_SOURCE_RX = 0x01`, `BRIDGE_SOURCE_TX = 0x02`) controlling which direction of mesh traffic gets forwarded to the bridge, checked in `examples/simple_repeater/MyMesh.cpp` (`logRx`/`logTx`). CLI: `set bridge.source logRx|logTx|logBoth` (default `logTx`).

### Bridge feature flags (`reply_data[8]` in login reply)

| Value | Bridge type |
|-------|-------------|
| `0x01` | RS232 UART |
| `0x02` | USB serial |
| `0x03` | ESP-NOW |
| `0x04` | TCP |
| `0x06` | MQTT |

### Enabling bridges in `platformio.local.ini`

```ini
# USB CDC serial (ESP32-S3 native USB)
build_flags = ${env.build_flags}
  -DWITH_USB_SERIAL_BRIDGE=Serial

# WiFi TCP on port 4403
build_flags = ${env.build_flags}
  -DWITH_TCP_BRIDGE=4403

# Hardware UART (existing)
build_flags = ${env.build_flags}
  -DWITH_RS232_BRIDGE=Serial2
  -DWITH_RS232_BRIDGE_RX=5
  -DWITH_RS232_BRIDGE_TX=6

# MQTT over WiFi
build_flags = ${env.build_flags}
  -DWITH_MQTT_BRIDGE=1
  -DMQTT_HOST='"mqtt.example.com"'
  -DMQTT_PACKET_TOPIC='"meshcore/bridge/mydomain/packets"'
  -DMQTT_STATUS_TOPIC='"meshcore/bridge/mydomain/status"'
  -DWIFI_SSID='"myssid"'
  -DWIFI_PWD='"mypasswd"'
lib_deps = ${env.lib_deps}
  knolleary/PubSubClient @ ^2.8
```

Only one bridge type may be active per build.

## Virtual Companion (`examples/virt_companion/`)

A Linux-native build of `companion_radio` that replaces the LoRa radio with an MQTT transport, for running MeshCore nodes as containers without physical hardware.

**Build/run:**
```sh
docker build -f examples/virt_companion/Dockerfile .   # run from repo root
```
The Dockerfile is Debian-based (`debian:bookworm-slim`), multi-stage (build + slim runtime), and links against `libmosquitto`.

**Config via environment variables:**

| Var | Purpose |
|-----|---------|
| `TCP_PORT` | Port for the companion TCP interface (default 5000) |
| `NODE_NAME` | Overrides the stored node name on every boot |
| `MQTT_HOST`, `MQTT_PORT` | MQTT broker to connect to |
| `MQTT_USERNAME`, `MQTT_PASSWORD` | MQTT auth |
| `MQTT_TOPIC` | Single shared topic used for all packet traffic |
| `LOCATION_LAT`, `LOCATION_LONG` | Static node location |

**Persistence:** all data (identity, contacts, channels, prefs, blobs) lives flat under `/data` — no per-node subdirectory. Each container instance is expected to have its own bind mount at `/data`. Identity keypair is a single file at `/data/identity`, generated on first boot if absent (via `radio_new_identity()` in `target.cpp`, seeded from `/dev/urandom`).

### How it connects to apps

Same framing as physical `companion_radio` boards, just over TCP instead of BLE/USB: `LinuxTCPInterface` (`src/helpers/linux/LinuxTCPInterface.h/.cpp`) implements `BaseSerialInterface` and speaks the standard companion frame protocol (`<`/`>` + 2-byte LE length prefix, see `docs/companion_protocol.md`) on `TCP_PORT`. Any companion app that talks TCP to a physical node can point at this container instead.

### How it talks to the MeshCore network

`MQTTRadio` (`src/helpers/linux/MQTTRadio.h/.cpp`) implements the `mesh::Radio` interface and substitutes for the LoRa driver used on real hardware:
- Raw MeshCore packets are hex-encoded (uppercase ASCII) and published/subscribed on a **single shared MQTT topic** (`MQTT_PACKET_TOPIC`) — there's no per-destination routing at the MQTT layer, every node on the topic sees every packet, same as a shared-air LoRa channel.
- Uses `libmosquitto`, with its own network loop thread (`mosquitto_loop_start`); inbound messages land in a small ring buffer (`recvRaw()` drains it) so the mesh loop stays non-blocking.
- Self-echo suppression: outbound packet hashes (FNV-1a) are tracked in a ring (`markSent`/`wasSentByUs`) so a node doesn't reprocess its own publish when it comes back via its own subscription.
- `getEstAirtimeFor()` fabricates a LoRa-like airtime estimate (`50 + len*3` ms) purely so the mesh scheduler's retransmission/backoff timing behaves like it would on real radio — MQTT delivery itself is near-instant.
- `packetScore()` always returns a perfect score (1.0) — no fading/interference simulation.
- No actual RF params exist; `setParams`/`setTxPower`/`setRxBoostedGainMode` are no-ops kept only to satisfy the `mesh::Radio` interface used by `CommonCLI`.

Practically: every virt_companion (and any bridge/gateway that also speaks this hex-over-MQTT convention) subscribed to the same `MQTT_PACKET_TOPIC` forms one mesh "radio domain," equivalent to a set of physical nodes sharing a LoRa frequency/channel.

### Linux shims (`examples/virt_companion/shims/`)

The core library assumes an Arduino environment. `shims/Arduino.h` and `shims/helpers/{IdentityStore,ClientACL}.h` provide POSIX/stdlib-backed stand-ins (timing, `Stream`/`File`/`FILESYSTEM` stubs, etc.) so unmodified `src/` code compiles for Linux. These shim directories **must** come before `src/` in the CMake include path — see `examples/virt_companion/CMakeLists.txt`.

## Virtual Room Server (`examples/virt_room_server/`)

A Linux-native build of `simple_room_server` (BBS-style chat server) that replaces the LoRa radio with an MQTT transport, for running room-server nodes as containers without physical hardware. Same MQTT-radio approach as Virtual Companion above, but admin access is a stdin/stdout console instead of a TCP companion interface, and it's Alpine-only (no Debian variant).

**Build/run:**
```sh
docker build -f examples/virt_room_server/Dockerfile .   # run from repo root
docker run -it -v /path/to/data:/data -e MQTT_HOST=... virt_room_server
```
The Dockerfile is Alpine-based (`alpine:3.20`), multi-stage (build + slim runtime), and links against `libmosquitto`. Since the CLI is stdin/stdout, use `-it` (or `docker attach` to a detached container) to interact with it.

**Config via environment variables:**

| Var | Purpose |
|-----|---------|
| `NODE_NAME` | Sets the node name, **first boot only** (see below) |
| `MQTT_HOST`, `MQTT_PORT` | MQTT broker to connect to |
| `MQTT_USERNAME`, `MQTT_PASSWORD` | MQTT auth |
| `MQTT_TOPIC` | Single shared topic used for all packet traffic |
| `LOCATION_LAT`, `LOCATION_LONG` | Static node location |

Admin/guest passwords are compile-time defines (`ADMIN_PASSWORD`, `ROOM_PASSWORD`), not env vars, since only `NODE_NAME` gets env-var override treatment.

**`NODE_NAME` is first-boot-only:** unlike Virtual Companion (where `NODE_NAME` overrides the stored name on *every* boot), the room server only applies `NODE_NAME` when no prefs have been persisted yet. `main.cpp` checks whether `/data/com_prefs` (or the legacy `/data/node_prefs`) exists *before* calling `the_mesh.begin(fs)` — if neither exists, this is a fresh install and `NODE_NAME` is applied and saved; otherwise the name already saved in `/data/com_prefs` always wins, regardless of what `NODE_NAME` is set to on that boot.

**Persistence:** all data lives flat under `/data` — no subdirectories are created. Identity is a single file at `/data/identity`, generated on first boot if absent. Node prefs, the client ACL, and the region map persist to `/data/com_prefs`, `/data/s_contacts`, and `/data/regions` respectively — all through the same unmodified `src/helpers/{CommonCLI,ClientACL,RegionMap}.cpp` used on real hardware, made to work against a real POSIX-backed `FILESYSTEM`/`File` shim (see below) rather than porting those classes.

### Admin CLI over stdin/stdout

There's no TCP/BLE companion interface — `examples/simple_room_server/main.cpp`'s existing serial CLI loop (buffer commands until `\r`, call `the_mesh.handleCommand()`, print the reply) runs as-is against a `HardwareSerial` shim backed by real stdin/stdout. stdin is set non-blocking (`fcntl(O_NONBLOCK)`) and polled (`poll()`) each loop iteration so the mesh/radio processing never stalls waiting on console input. Interact with the running container via `docker run -it` or `docker attach`.

### Linux shims (`examples/virt_room_server/shims/`)

Same idea as Virtual Companion's shims, but the `FILESYSTEM`/`File` types in `shims/Arduino.h` are **real POSIX-backed implementations** (wrapping `fopen`/`fread`/`fwrite`/etc. rooted at `/data`) rather than no-op stubs. This lets `CommonCLI.cpp`, `RegionMap.cpp`, `ClientACL.cpp`, and `IdentityStore.cpp` compile and run completely unmodified from `src/helpers/`, since they already only depend on the generic `FILESYSTEM*`/`File` interface (the same one Arduino platforms implement with their native filesystem). `shims/Arduino.h`'s `Stream::readBytes` is `virtual` (unlike Virtual Companion's shim) so `File` can override it. `shims/` must come before `src/` in the CMake include path — see `examples/virt_room_server/CMakeLists.txt`.

## Documentation

Protocol and format specs are in `docs/`:
- `packet_format.md` — wire format details
- `companion_protocol.md` — BLE/USB companion app protocol
- `payloads.md` — message payload structures
- `kiss_modem_protocol.md` — KISS serial protocol
- `cli_commands.md` — CLI command reference
