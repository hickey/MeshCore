#pragma once

#ifdef WITH_MQTT_BRIDGE

#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif

#ifndef MQTT_PACKET_TOPIC
  #define MQTT_PACKET_TOPIC "meshcore/bridge/default/packets"
#endif

#ifndef MQTT_STATUS_TOPIC
  #define MQTT_STATUS_TOPIC "meshcore/bridge/default/status"
#endif

#ifndef MQTT_STATUS_INTERVAL
  #define MQTT_STATUS_INTERVAL 60
#endif

// MQTT_MAX_PACKET_SIZE must fit: MQTT headers + topic + hex payload
// Max hex payload = (MAX_TRANS_UNIT+1)*2 = 370 chars; add topic and MQTT overhead
#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 512
#endif

#include "helpers/bridges/BridgeBase.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Mesh.h>

#if MQTT_DEBUG && ARDUINO
  #define MQTT_DEBUG_PRINTLN(F, ...) Serial.printf("%s MQTT: " F "\n", getLogDateTime(), ##__VA_ARGS__)
#else
  #define MQTT_DEBUG_PRINTLN(...) {}
#endif

/**
 * @brief Bridge implementation using MQTT over WiFi for packet transport
 *
 * Publishes and subscribes to an MQTT topic, bridging LoRa mesh packets
 * to/from the MQTT broker. Packets are encoded as uppercase ASCII hex strings.
 *
 * Configuration build defines:
 *   WITH_MQTT_BRIDGE      — enable this bridge
 *   MQTT_HOST             — broker hostname or IP (required)
 *   MQTT_PORT             — broker port (default 1883)
 *   MQTT_USERNAME         — broker username (optional, omit to disable auth)
 *   MQTT_PASSWORD         — broker password (optional)
 *   MQTT_PACKET_TOPIC     — topic to publish/subscribe (default "meshcore/bridge/default/packets")
 *   MQTT_STATUS_TOPIC     — topic to publish status (default "meshcore/bridge/default/status")
 *   MQTT_STATUS_INTERVAL  — status publish interval in seconds (default 60)
 *   MQTT_DEBUG            — set to 1 to enable debug output on Serial
 *
 * WiFi must be configured separately via WIFI_SSID / WIFI_PWD build defines,
 * or by calling WiFi.begin() before this bridge's begin() is invoked. The
 * bridge will not attempt an MQTT connection until WiFi is associated.
 *
 * Packet format: raw wire bytes encoded as uppercase hex, e.g.
 *   "1180EF4C22171A42..."
 * Incoming messages on the topic are decoded and injected into the mesh.
 * Duplicate detection via SimpleMeshTables prevents re-forwarding seen packets.
 */
class MQTTBridge : public BridgeBase {
public:
  MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, const uint8_t *pubKey);

  char mqtt_host[64];
  uint mqtt_port;
  char mqtt_packet_topic[64];
  char mqtt_status_topic[64];

  void begin() override;
  void end() override;
  void loop() override;
  void sendPacket(mesh::Packet *packet) override;
  void onPacketReceived(mesh::Packet *packet) override;
  void initialize();
  bool reconnect();
  void setMesh(mesh::Mesh *mesh) { _mesh = mesh; }

  template<typename RadioDriverType>
  void setRadioDriver(RadioDriverType *driver) {
    _radio_driver = (void*)driver;
    _getPacketsRecv = [](void* d) { return ((RadioDriverType*)d)->getPacketsRecv(); };
    _getPacketsSent = [](void* d) { return ((RadioDriverType*)d)->getPacketsSent(); };
    _getPacketsRecvErrors = [](void* d) { return ((RadioDriverType*)d)->getPacketsRecvErrors(); };
    _getLastRSSI = [](void* d) { return ((RadioDriverType*)d)->getLastRSSI(); };
    _getLastSNR = [](void* d) { return ((RadioDriverType*)d)->getLastSNR(); };
  }

  void setMillisecondClock(mesh::MillisecondClock *ms) { _ms = ms; }
  void setRadio(mesh::Radio *radio) { _radio = radio; }

  const char* buildStatusMessage(const char *status);

private:
  static MQTTBridge *_instance;

  mesh::Mesh *_mesh;
  mesh::MillisecondClock *_ms;
  mesh::Radio *_radio;
  void *_radio_driver;

  // Type-erased function pointers for radio driver stats
  uint32_t (*_getPacketsRecv)(void*);
  uint32_t (*_getPacketsSent)(void*);
  uint32_t (*_getPacketsRecvErrors)(void*);
  int16_t (*_getLastRSSI)(void*);
  float (*_getLastSNR)(void*);

  WiFiClient _wifiClient;
  PubSubClient _mqttClient;
  unsigned long _lastReconnectAttempt;
  unsigned long _lastStatusPublish;
  const uint8_t *_pubKey;

  char _mqtt_username[32];
  char _mqtt_password[64];

  // Buffer sized for maximum hex-encoded mesh packet
  static constexpr size_t HEX_BUF_SIZE = (MAX_TRANS_UNIT + 1) * 2 + 1;
  char _hexBuf[HEX_BUF_SIZE];

  // Buffer for status messages
  static constexpr size_t STATUS_BUF_SIZE = 512;
  char _statusBuf[STATUS_BUF_SIZE];

  static void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);
};

#endif
