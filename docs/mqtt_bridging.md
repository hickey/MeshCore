# MQTT Bridge Documentation

## Overview

The MQTT bridge enables MeshCore nodes to communicate over WiFi and MQTT in addition to (or instead of) LoRa radio. Packets are hex-encoded and published to a shared MQTT topic, allowing nodes to bridge between LoRa mesh networks and MQTT-based infrastructure.

## How It Works

### Packet Transport

- **Encoding**: Raw mesh packets are encoded as uppercase ASCII hex strings (e.g., `1180EF4C22171A42...`)
- **Topics**: Packets are published/subscribed on `MQTT_PACKET_TOPIC`
- **Shared Channel**: All nodes subscribed to the same packet topic form one mesh "radio domain," similar to nodes sharing a LoRa frequency
- **Duplicate Suppression**: `SimpleMeshTables` tracks seen packets to prevent re-forwarding and self-echo

### Status Publishing

When a node connects to the MQTT broker, it publishes a comprehensive status message:
- **Topic**: `MQTT_STATUS_TOPIC/<pubkey_hex>` (e.g., `meshcore/bridge/default/status/A1B2C3D4E5F6...`)
- **Payload**: JSON object with status and statistics (see below)
- **Retain Flag**: Set to `true` so subscribers can see the last known status
- **Update Interval**: Republished periodically at the interval set by `MQTT_STATUS_INTERVAL` (default 60 seconds)

#### Status Message Fields

The status message includes the following fields (when available):

| Field | Description |
|-------|-------------|
| `status` | Connection status: "online" when bridge_source is non-zero, "not-bridging" when bridge_source is 0, or "offline" (LWT) |
| `bridge_source` | Bridge direction setting: "rx", "tx", "both", or "none" |
| `uptime_secs` | Node uptime in seconds |
| `tx_air_secs` | Total transmit airtime in seconds |
| `rx_air_secs` | Total receive airtime in seconds |
| `tx_packets` | Total packets transmitted (flood + direct) |
| `tx_flood` | Flood-routed packets transmitted |
| `tx_direct` | Direct-routed packets transmitted |
| `rx_packets` | Total packets received (flood + direct) |
| `rx_flood` | Flood-routed packets received |
| `rx_direct` | Direct-routed packets received |
| `dup_flood` | Duplicate flood packets detected |
| `dup_direct` | Duplicate direct packets detected |
| `noise_floor` | Current RF noise floor (dBm) |
| `last_rssi` | Last received signal strength (dBm) |
| `last_snr` | Last signal-to-noise ratio |
| `rx_errors` | Received packet errors |
| `tx_queue_len` | Current transmit queue length |
| `clock` | Unix timestamp from RTC |

**Example online status message:**
```json
{
  "status": "online",
  "bridge_source": "both",
  "uptime_secs": 3600,
  "tx_air_secs": 45,
  "rx_air_secs": 120,
  "tx_packets": 150,
  "tx_flood": 100,
  "tx_direct": 50,
  "rx_packets": 320,
  "rx_flood": 280,
  "rx_direct": 40,
  "dup_flood": 15,
  "dup_direct": 3,
  "noise_floor": -110,
  "last_rssi": -85,
  "last_snr": 8.5,
  "rx_errors": 2,
  "tx_queue_len": 0,
  "clock": 1724198400
}
```

### Last Will and Testament (LWT)

The bridge configures an MQTT Last Will and Testament when connecting:
- **Topic**: Same as online status (`MQTT_STATUS_TOPIC/<pubkey_hex>`)
- **Payload**: `{"status":"offline"}`
- **Retain Flag**: Set to `true`
- **Behavior**: Automatically published by the broker when the client disconnects unexpectedly (network failure, crash, etc.)

This allows monitoring systems to track when nodes go offline, even if the node cannot send a graceful disconnect message.

### Client Identification

Each bridge instance uses a unique MQTT client ID derived from its public key:
- Format: `meshcore-mqtt-bridge-<hex6>` (e.g., `meshcore-mqtt-bridge-A1B2C3`)

### Connection Behavior

- WiFi must be connected before MQTT connection attempts
- Automatic reconnection every 5 seconds if disconnected
- WiFi can be configured via build defines or manually before `bridge.begin()`

### Zero-Hop Advertisement Handling

Zero-hop advertisements (`PAYLOAD_TYPE_ADVERT`, `ROUTE_TYPE_DIRECT`, `path_len == 0`) received over MQTT require special handling:

- Normal receive path only retransmits flood-route packets
- Zero-hop adverts from other nodes are passed directly to `Mesh::sendZeroHop()`
- This transmits them as genuine zero-hop packets over LoRa, visible only to direct radio neighbors
- Prevents MQTT-sourced adverts from being flood-retransmitted across the entire mesh

## Adding MQTT Bridge to a PlatformIO Project

### Step 1: Add the Library Dependency

In your `platformio.ini` or `platformio.local.ini`, add the PubSubClient library:

```ini
lib_deps =
  knolleary/PubSubClient @ ^2.8
```

### Step 2: Configure Build Flags

Add the required build flags to enable the bridge and configure connection settings:

```ini
build_flags = ${env.build_flags}
  -DWITH_MQTT_BRIDGE=1
  -DMQTT_HOST='"mqtt.example.com"'
  -DMQTT_PACKET_TOPIC='"meshcore/bridge/mydomain/packets"'
  -DMQTT_STATUS_TOPIC='"meshcore/bridge/mydomain/status"'
  -DWIFI_SSID='"your_wifi_ssid"'
  -DWIFI_PWD='"your_wifi_password"'
```

**Note**: String values must be wrapped in escaped quotes (`'"value"'`) because they are C preprocessor defines.

### Step 3: Instantiate the Bridge in Your Code

```cpp
#include <helpers/bridges/MQTTBridge.h>

// In your mesh setup code:
MQTTBridge bridge(&prefs, &packetManager, &rtcClock, pubKey);

// If using zero-hop advert retransmission, provide mesh pointer:
bridge.setMesh(&mesh);

// Start the bridge
bridge.begin();

// In your main loop:
void loop() {
  bridge.loop();
  // ... other loop code
}
```

### Step 4: Runtime Configuration via CLI

If your firmware includes `CommonCLI`, MQTT settings can be changed at runtime:

```
set mqtt.host mqtt.newbroker.com
set mqtt.port 1883
set mqtt.packet.topic meshcore/bridge/domain/packets
set mqtt.status.topic meshcore/bridge/domain/status
```

Each change tears down and reinitializes the MQTT client connection.

## Build Definitions Reference

| Build Definition | Default Value | Required | Description |
|-----------------|---------------|----------|-------------|
| `WITH_MQTT_BRIDGE` | *(none)* | **Yes** | Set to `1` to enable the MQTT bridge feature |
| `MQTT_HOST` | *(none)* | **Yes** | MQTT broker hostname or IP address (e.g., `"mqtt.example.com"`) |
| `MQTT_PORT` | `1883` | No | MQTT broker port number |
| `MQTT_USERNAME` | *(none)* | No | MQTT broker username for authentication. Omit for anonymous connection |
| `MQTT_PASSWORD` | *(none)* | No | MQTT broker password for authentication |
| `MQTT_PACKET_TOPIC` | `"meshcore/bridge/default/packets"` | No | MQTT topic for mesh packet exchange. All nodes on the same topic form one mesh domain |
| `MQTT_STATUS_TOPIC` | `"meshcore/bridge/default/status"` | No | MQTT topic base for status messages. Each node appends `/<pubkey_hex>` |
| `MQTT_STATUS_INTERVAL` | `60` | No | Status publish interval in seconds. How often status updates are published to the broker |
| `MQTT_DEBUG` | `0` | No | Set to `1` to enable debug output to `Serial` |
| `MQTT_MAX_PACKET_SIZE` | `512` | No | Maximum MQTT packet size in bytes. Must accommodate MQTT headers + topic + hex payload (max 370 chars) |
| `WIFI_SSID` | *(none)* | Recommended | WiFi network SSID. If omitted, WiFi must be started manually before `bridge.begin()` |
| `WIFI_PWD` | *(none)* | No | WiFi network password. Omit for open networks |

## Bridge Source Control

The `bridge_pkt_src` preference (configurable via CLI) controls which mesh traffic directions are forwarded to the bridge:

| CLI Command | Description |
|-------------|-------------|
| `set bridge.source logRx` | Forward only received packets (RX) |
| `set bridge.source logTx` | Forward only transmitted packets (TX) *(default)* |
| `set bridge.source logBoth` | Forward both RX and TX packets |

This is controlled by a bitmask:
- `BRIDGE_SOURCE_RX = 0x01`
- `BRIDGE_SOURCE_TX = 0x02`

## Example Configurations

### Basic MQTT Bridge (Anonymous Connection)

```ini
[env:my_device]
build_flags = ${env.build_flags}
  -DWITH_MQTT_BRIDGE=1
  -DMQTT_HOST='"192.168.1.100"'
  -DWIFI_SSID='"MyNetwork"'
  -DWIFI_PWD='"MyPassword"'
lib_deps = ${env.lib_deps}
  knolleary/PubSubClient @ ^2.8
```

### MQTT Bridge with Authentication and Custom Topics

```ini
[env:my_device]
build_flags = ${env.build_flags}
  -DWITH_MQTT_BRIDGE=1
  -DMQTT_HOST='"mqtt.example.com"'
  -DMQTT_PORT=8883
  -DMQTT_USERNAME='"bridge_user"'
  -DMQTT_PASSWORD='"secure_password"'
  -DMQTT_PACKET_TOPIC='"meshcore/bridge/production/packets"'
  -DMQTT_STATUS_TOPIC='"meshcore/bridge/production/status"'
  -DWIFI_SSID='"ProductionWiFi"'
  -DWIFI_PWD='"WiFiPassword"'
lib_deps = ${env.lib_deps}
  knolleary/PubSubClient @ ^2.8
```

### MQTT Bridge with Debug Output

```ini
[env:my_device]
build_flags = ${env.build_flags}
  -DWITH_MQTT_BRIDGE=1
  -DMQTT_HOST='"mqtt.local"'
  -DMQTT_DEBUG=1
  -DWIFI_SSID='"TestNetwork"'
  -DWIFI_PWD='"test123"'
lib_deps = ${env.lib_deps}
  knolleary/PubSubClient @ ^2.8
```

## Integration with Other Bridges

**Important**: Only one bridge type may be active per build. The following bridges are mutually exclusive:
- `WITH_MQTT_BRIDGE`
- `WITH_RS232_BRIDGE`
- `WITH_USB_SERIAL_BRIDGE`
- `WITH_TCP_BRIDGE`
- `WITH_ESPNOW_BRIDGE`

## Monitoring Bridge Status

Subscribe to the status topic to monitor online nodes:

```bash
mosquitto_sub -h mqtt.example.com -t "meshcore/bridge/default/status/#" -v
```

Output will show:
```
meshcore/bridge/default/status/A1B2C3D4E5F6... {"status":"online"}
meshcore/bridge/default/status/1A2B3C4D5E6F... {"status":"online"}
```

## Troubleshooting

### Bridge Not Connecting

1. **Check WiFi Connection**: Ensure WiFi is connected before MQTT connection attempts
2. **Verify Broker Settings**: Confirm `MQTT_HOST` and `MQTT_PORT` are correct
3. **Check Authentication**: Verify `MQTT_USERNAME` and `MQTT_PASSWORD` if required
4. **Enable Debug Output**: Set `MQTT_DEBUG=1` to see connection attempts

### Packets Not Being Forwarded

1. **Check Bridge Source Setting**: Verify `bridge.source` is configured correctly (`logRx`, `logTx`, or `logBoth`)
2. **Check Topic Configuration**: Ensure all nodes use the same `MQTT_PACKET_TOPIC`
3. **Monitor MQTT Traffic**: Use `mosquitto_sub` to verify packets are being published

### Debug Output Examples

With `MQTT_DEBUG=1`, you'll see messages like:

```
2026-08-20 10:30:45 MQTT: Initialized, broker=mqtt.example.com:1883 packet_topic=meshcore/bridge/default/packets status_topic=meshcore/bridge/default/status
2026-08-20 10:30:46 MQTT: Connecting to mqtt.example.com:1883...
2026-08-20 10:30:46 MQTT: Connected, subscribed to meshcore/bridge/default/packets
2026-08-20 10:30:46 MQTT: Published status to meshcore/bridge/default/status/A1B2C3D4E5F6...
2026-08-20 10:30:50 MQTT: PUB raw=1180EF4C22171A42...
2026-08-20 10:30:51 MQTT: SUB raw=2290AB5C33282B53...
```

## Virtual Companion and Room Server

The MQTT bridge is also used by Linux-native builds:
- **Virtual Companion** (`examples/virt_companion/`) - Containerized companion radio
- **Virtual Room Server** (`examples/virt_room_server/`) - Containerized BBS-style chat server

These use the same hex-over-MQTT protocol and can interoperate with physical MQTT bridge nodes.
