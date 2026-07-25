# Virtual Room Server

A Linux-native build of [`simple_room_server`](../simple_room_server) (BBS-style
chat server) that replaces the LoRa radio with an MQTT transport, so a room
server can run as a container without any physical hardware.

It's not a PlatformIO firmware build — it's a plain CMake project compiled
for Linux, packaged as an Alpine Docker image. Anywhere `simple_room_server`
would talk to nearby nodes over LoRa, this instead publishes/subscribes
MeshCore packets on a shared MQTT topic (see [How it talks to the MeshCore
network](#how-it-talks-to-the-meshcore-network) below).

Admin/guest access works exactly like a physical room server — the same CLI
commands, just reachable over `stdin`/`stdout` instead of a serial port. See
[CLI Commands](../../docs/cli_commands.md) for the full command reference.

## Building

```sh
# from the repo root
docker build -f examples/virt_room_server/Dockerfile -t virt_room_server .
```

Multi-stage build: `alpine:3.20` with `cmake`/`g++`/`mosquitto-dev` compiles
the binary, then a slim `alpine:3.20` runtime stage with only
`libstdc++`/`libgcc`/`mosquitto-libs` is used to run it.

## Running

```sh
docker run -it \
  -v /path/to/data:/data \
  -e MQTT_HOST=mqtt.example.com \
  -e NODE_NAME=MyRoomServer \
  virt_room_server
```

Use `-it` (or `docker attach` on a container started with `-d`) since the
admin/guest CLI is a `stdin`/`stdout` console — there's no TCP/BLE companion
interface for this build.

Always bind-mount `/data` to a host directory (or named volume) so identity
and node state survive container restarts/recreation.

## Configuration

All runtime configuration is via environment variables:

| Var | Purpose |
|-----|---------|
| `NODE_NAME` | Sets the node name, **first boot only** (see below) |
| `MQTT_HOST` | MQTT broker hostname/IP to connect to |
| `MQTT_PORT` | MQTT broker port (default `1883`) |
| `MQTT_USERNAME` | MQTT auth username (optional) |
| `MQTT_PASSWORD` | MQTT auth password (optional) |
| `MQTT_TOPIC` | Single shared MQTT topic used for all packet traffic (default `meshcore/packets`) |
| `LOCATION_LAT` | Static node latitude |
| `LOCATION_LONG` | Static node longitude |

Admin and guest/room passwords are **compile-time defines**, not env vars —
`ADMIN_PASSWORD` (default `"password"`) and `ROOM_PASSWORD` (undefined by
default, meaning no guest password is required). Change them by adding
`-DADMIN_PASSWORD='"..."'` / `-DROOM_PASSWORD='"..."'` to the `cmake` build
step, or by using the CLI's `set password`/`set guest.password` commands
after boot (persisted to `/data/com_prefs`).

### `NODE_NAME` is first-boot-only

Unlike Virtual Companion (where `NODE_NAME` overrides the stored name on
*every* boot), the room server only applies `NODE_NAME` when no prefs have
been persisted yet. `main.cpp` checks whether `/data/com_prefs` (or the
legacy `/data/node_prefs`) exists *before* the mesh engine loads/creates
those prefs — if neither file exists, this is a fresh install and
`NODE_NAME` is applied and saved. Otherwise, the name already saved in
`/data/com_prefs` always wins, regardless of what `NODE_NAME` is set to on
that boot.

## Persistence

All data lives flat under `/data` — no subdirectories are created:

| File | Contents |
|------|----------|
| `/data/identity` | Ed25519 keypair identity, generated on first boot if absent |
| `/data/com_prefs` | Node preferences (name, passwords, radio-sim params, etc.) |
| `/data/s_contacts` | Client ACL / contact list |
| `/data/regions` | Region map |

These go through the same unmodified `src/helpers/{CommonCLI,ClientACL,RegionMap}.cpp`
used on real hardware, backed by a real POSIX-backed `FILESYSTEM`/`File` shim
(see [Linux shims](#linux-shims) below) instead of a hardware filesystem.

## Admin CLI over stdin/stdout

There's no TCP/BLE companion interface for this build — the same serial CLI
loop used by `simple_room_server` (buffer input until `\r`, dispatch via
`handleCommand()`, print the reply) runs as-is against a `HardwareSerial`
shim backed by real stdin/stdout. `stdin` is set non-blocking and polled each
loop iteration so mesh/radio processing never stalls waiting on console
input. Interact with a running container via `docker run -it` or
`docker attach`.

## How it talks to the MeshCore network

`MQTTRadio` (`src/helpers/linux/MQTTRadio.h/.cpp`) implements the
`mesh::Radio` interface in place of the LoRa driver used on real hardware:

- Raw MeshCore packets are hex-encoded and published/subscribed on a single
  shared MQTT topic (`MQTT_TOPIC`) — every node on the topic sees every
  packet, the same as a shared-air LoRa channel.
- The MQTT client ID is derived from the node's own identity:
  `meshcore-room-server-<3 byte pubkey prefix>`, so multiple containers can
  connect to the same broker without client ID collisions.
- `getEstAirtimeFor()` fabricates a LoRa-like airtime estimate purely so the
  mesh scheduler's retransmission/backoff timing behaves like it would on
  real radio — MQTT delivery itself is near-instant.
- No actual RF params exist; radio-parameter setters are no-ops kept only to
  satisfy the `mesh::Radio` interface.

Any other virtual room server, [Virtual Companion](../virt_companion), or
bridge/gateway speaking this hex-over-MQTT convention on the same
`MQTT_TOPIC` forms one mesh "radio domain," equivalent to a set of physical
nodes sharing a LoRa frequency/channel.

## Linux shims (`shims/`)

The core library assumes an Arduino environment. `shims/Arduino.h` provides
POSIX/stdlib-backed stand-ins (timing, `HardwareSerial`, and a real
POSIX-backed `FILESYSTEM`/`File` wrapping `fopen`/`fread`/`fwrite`/etc.
rooted at `/data`) so unmodified `src/` code compiles and runs for Linux.
`shims/` must come before `src/` in the CMake include path — see
[`CMakeLists.txt`](./CMakeLists.txt).
