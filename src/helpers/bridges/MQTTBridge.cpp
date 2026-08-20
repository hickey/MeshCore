#include "MQTTBridge.h"
#include "Identity.h"
#include <helpers/CommonCLI.h>
#include <helpers/esp32/SerialWifiInterface.h>
#include <helpers/TxtDataHelpers.h>
#include <string>

#ifdef WITH_MQTT_BRIDGE

#ifndef MQTT_HOST
  #error "WITH_MQTT_BRIDGE requires MQTT_HOST to be defined"
#endif

#ifndef WIFI_SSID
  #warning "WITH_MQTT_BRIDGE: WIFI_SSID not defined — WiFi must be started externally before bridge.begin()"
#endif

#include <Arduino.h>

MQTTBridge *MQTTBridge::_instance = nullptr;

MQTTBridge::MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, const uint8_t *pubKey)
    : BridgeBase(prefs, mgr, rtc), _mesh(nullptr), _ms(nullptr), _radio(nullptr), _tables(nullptr), _board(nullptr), _radio_driver(nullptr),
      _getPacketsRecv(nullptr), _getPacketsSent(nullptr), _getPacketsRecvErrors(nullptr),
      _getLastRSSI(nullptr), _getLastSNR(nullptr),
      _mqttClient(_wifiClient), _lastReconnectAttempt(0), _lastStatusPublish(0), _pubKey(pubKey) {
  _instance = this;
}

void MQTTBridge::begin() {
#if defined(WIFI_SSID)
  if (WiFi.status() != WL_CONNECTED) {
    WIFI_DEBUG_PRINTLN("Starting WiFi SSID=%s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
#if defined(WIFI_PWD)
    WiFi.begin(WIFI_SSID, WIFI_PWD);
#else
    WiFi.begin(WIFI_SSID);
#endif
  }
#else
  if (WiFi.status() != WL_CONNECTED) {
    WIFI_DEBUG_PRINTLN("WiFi not connected — will retry when WiFi is available");
  }
#endif

  // save MQTT settings for access from CLI
  StrHelper::strzcpy(mqtt_host, MQTT_HOST, sizeof(mqtt_host));
  mqtt_port = MQTT_PORT;
  StrHelper::strzcpy(_mqtt_username, MQTT_USERNAME, sizeof(_mqtt_username));
  StrHelper::strzcpy(_mqtt_password, MQTT_PASSWORD, sizeof(_mqtt_password));
  StrHelper::strzcpy(mqtt_packet_topic, MQTT_PACKET_TOPIC, sizeof(mqtt_packet_topic));
  StrHelper::strzcpy(mqtt_status_topic, MQTT_STATUS_TOPIC, sizeof(mqtt_status_topic));

  initialize();
}

void MQTTBridge::initialize() {
  _mqttClient.setServer(mqtt_host, mqtt_port);
  _mqttClient.setCallback(mqttCallback);
  _mqttClient.setBufferSize(1024);  // Increased for larger status messages

  _initialized = true;
  MQTT_DEBUG_PRINTLN("Initialized, broker=%s:%d packet_topic=%s status_topic=%s",
                     mqtt_host, mqtt_port, mqtt_packet_topic, mqtt_status_topic);
}

void MQTTBridge::end() {
  if (_mqttClient.connected()) {
    _mqttClient.disconnect();
  }
  _initialized = false;
  MQTT_DEBUG_PRINTLN("Stopped");
}

const char* MQTTBridge::buildStatusMessage(const char *status) {
  int offset = snprintf(_statusBuf, STATUS_BUF_SIZE, "{\"status\": \"%s\"", status);

  // Add node name
  if (_prefs && _prefs->node_name[0] != '\0') {
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"node_name\": \"%s\"", _prefs->node_name);
  }

  // For offline status, skip all other fields
  if (strcmp(status, "offline") == 0) {
    snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "}");
    return _statusBuf;
  }

  // Add model (from build define)
#ifdef ADVERT_NAME
  offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"model\": \"%s\"", ADVERT_NAME);
#endif

  // Add firmware version (from build define if available)
#ifdef FIRMWARE_VERSION
  offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"firmware_version\": \"%s\"", FIRMWARE_VERSION);
#endif

  // Add radio settings (freq, bw, sf, cr)
  if (_prefs) {
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset,
                      ", \"radio_settings\": \"%.3f, %.1f, %u, %u\"",
                      _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  }

  // Add bridge source mode
  const char *bridge_source_str;
  uint8_t bridge_src = _prefs->bridge_pkt_src;
  if ((bridge_src & BRIDGE_SOURCE_RX) && (bridge_src & BRIDGE_SOURCE_TX)) {
    bridge_source_str = "both";
  } else if (bridge_src & BRIDGE_SOURCE_RX) {
    bridge_source_str = "rx";
  } else if (bridge_src & BRIDGE_SOURCE_TX) {
    bridge_source_str = "tx";
  } else {
    bridge_source_str = "none";
  }
  offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"bridge_source\": \"%s\"", bridge_source_str);

  // Add uptime if millisecond clock available (human readable format)
  if (_ms) {
    uint32_t uptime_secs = (uint32_t)(_ms->getMillis() / 1000);
    char uptime_str[32];
    uint32_t mins = uptime_secs / 60;
    uint32_t hours = uptime_secs / 3600;
    uint32_t days = uptime_secs / 84600;

    snprintf(uptime_str, sizeof(uptime_str), "%ud%dh%um", days, hours, mins);
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"uptime\": \"%s\"", uptime_str);
  }

  // Add RTC clock timestamp if available
  if (_rtc) {
    uint32_t timestamp = _rtc->getCurrentTime();
    time_t t = (time_t)timestamp;
    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"clock\": \"%s\"", timeBuf);
  }

  // Add stats object with mesh and radio statistics
  offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"stats\": {");

  bool first_stat = true;

  // Add mesh statistics if available
  if (_mesh) {
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "\"tx_air_secs\": %u", (uint32_t)(_mesh->getTotalAirTime() / 1000));
    first_stat = false;

    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"rx_air_secs\": %u", (uint32_t)(_mesh->getReceiveAirTime() / 1000));
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"tx_packets\": %u", _mesh->getNumSentFlood() + _mesh->getNumSentDirect());
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"tx_flood\": %u", _mesh->getNumSentFlood());
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"tx_direct\": %u", _mesh->getNumSentDirect());
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"rx_packets\": %u", _mesh->getNumRecvFlood() + _mesh->getNumRecvDirect());
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"rx_flood\": %u", _mesh->getNumRecvFlood());
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"rx_direct\": %u", _mesh->getNumRecvDirect());

    // Add duplicate packet counts if SimpleMeshTables available
    if (_tables) {
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"dup_flood\": %u", _tables->getNumFloodDups());
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"dup_direct\": %u", _tables->getNumDirectDups());
    }
  }

  // Add radio statistics if available
  if (_radio) {
    if (!first_stat) {
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", ");
    }
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "\"noise_floor\": %d", (int16_t)_radio->getNoiseFloor());
    first_stat = false;
  }

  // Add radio driver statistics if available
  if (_radio_driver && _getLastRSSI && _getLastSNR && _getPacketsRecvErrors) {
    if (!first_stat) {
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", ");
    }
    float rssi_raw = _getLastRSSI(_radio_driver);
    float rssi_scaled = rssi_raw / 100000000.0f;
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "\"last_rssi\": %.2f", rssi_scaled);
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"last_snr\": %.2f", _getLastSNR(_radio_driver));
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"rx_errors\": %u", _getPacketsRecvErrors(_radio_driver));
    first_stat = false;
  }

  // Add packet manager queue length
  if (_mgr) {
    if (!first_stat) {
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", ");
    }
    offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "\"tx_queue_len\": %u", _mgr->getOutboundTotal());
    first_stat = false;
  }

  // Add battery voltage and percentage
  if (_board) {
    uint16_t batt_mv = _board->getBattMilliVolts();
    if (batt_mv > 0) {
      if (!first_stat) {
        offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", ");
      }
      float batt_volts = batt_mv / 1000.0f;
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "\"battery_voltage\": %.3f", batt_volts);

      // Calculate battery percentage (LiPo: 4.2V = 100%, 3.0V = 0%)
      uint8_t batt_pct = 0;
      if (batt_mv >= 4200) {
        batt_pct = 100;
      } else if (batt_mv <= 3000) {
        batt_pct = 0;
      } else {
        batt_pct = (uint8_t)(((batt_mv - 3000) * 100) / 1200);
      }
      offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, ", \"battery_percent\": %u", batt_pct);
      first_stat = false;
    }
  }

  // Close stats object
  offset += snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "}");

  snprintf(_statusBuf + offset, STATUS_BUF_SIZE - offset, "}");
  return _statusBuf;
}

bool MQTTBridge::reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    MQTT_DEBUG_PRINTLN("WiFi not connected, skipping MQTT reconnect");
    return false;
  }

  MQTT_DEBUG_PRINTLN("Connecting to %s:%d...", mqtt_host, mqtt_port);

  char clientId[28]; // "meshcore-mqtt-bridge-" (21) + 6 hex chars + null
  snprintf(clientId, sizeof(clientId), "meshcore-mqtt-bridge-%02X%02X%02X",
           _pubKey[0], _pubKey[1], _pubKey[2]);

  // Build status topic with pubkey
  char statusTopic[128];
  char pubkeyHex[PUB_KEY_SIZE * 2 + 1];
  for (uint8_t i = 0; i < PUB_KEY_SIZE; i++) {
    snprintf(&pubkeyHex[i * 2], 3, "%02X", _pubKey[i]);
  }
  snprintf(statusTopic, sizeof(statusTopic), "%s/%s", mqtt_status_topic, pubkeyHex);

  bool ok;
  const char *offlineMsg = buildStatusMessage("offline");

#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
  ok = _mqttClient.connect(clientId, _mqtt_username, _mqtt_password, statusTopic, 0, true, offlineMsg);
#elif defined(MQTT_USERNAME)
  ok = _mqttClient.connect(clientId, _mqtt_username, nullptr, statusTopic, 0, true, offlineMsg);
#else
  ok = _mqttClient.connect(clientId, nullptr, nullptr, statusTopic, 0, true, offlineMsg);
#endif

  if (ok) {
    _mqttClient.subscribe(mqtt_packet_topic);
    MQTT_DEBUG_PRINTLN("Connected, subscribed to %s", mqtt_packet_topic);

    // Publish status as online with statistics
    if (_mqttClient.publish(statusTopic, buildStatusMessage("online"), true)) {
      MQTT_DEBUG_PRINTLN("Published status to %s", statusTopic);
    } else {
      MQTT_DEBUG_PRINTLN("Status publish failed");
    }
  } else {
    MQTT_DEBUG_PRINTLN("Connect failed, rc=%d", _mqttClient.state());
  }
  return ok;
}

void MQTTBridge::loop() {
  if (!_initialized) return;

  if (!_mqttClient.connected()) {
    unsigned long now = millis();
    // Retry every 5 seconds
    if (now - _lastReconnectAttempt >= 5000) {
      _lastReconnectAttempt = now;
      reconnect();
    }
  } else {
    _mqttClient.loop();

    // Publish status update at configured interval
    unsigned long now = millis();
    if (now - _lastStatusPublish >= (MQTT_STATUS_INTERVAL * 1000)) {
      _lastStatusPublish = now;

      // Build status topic with pubkey
      char statusTopic[128];
      char pubkeyHex[PUB_KEY_SIZE * 2 + 1];
      for (uint8_t i = 0; i < PUB_KEY_SIZE; i++) {
        snprintf(&pubkeyHex[i * 2], 3, "%02X", _pubKey[i]);
      }
      snprintf(statusTopic, sizeof(statusTopic), "%s/%s", mqtt_status_topic, pubkeyHex);

      // Set status based on bridge source setting
      const char *status = (_prefs->bridge_pkt_src != 0) ? "online" : "not-bridging";

      if (_mqttClient.publish(statusTopic, buildStatusMessage(status), true)) {
        MQTT_DEBUG_PRINTLN("Published periodic status update");
      } else {
        MQTT_DEBUG_PRINTLN("Periodic status publish failed");
      }
    }
  }
}

void MQTTBridge::sendPacket(mesh::Packet *packet) {
  if (!_initialized || !packet) return;

  if (!_mqttClient.connected()) return;

  if (!_seen_packets.wasSeen(packet)) {
    uint8_t buf[MAX_TRANS_UNIT + 1];
    uint16_t len = packet->writeTo(buf);

    if (len == 0 || len > sizeof(buf)) {
      MQTT_DEBUG_PRINTLN("PUB invalid packet length %d", len);
      return;
    }

    // Encode as uppercase hex
    static const char *hex_chars = "0123456789ABCDEF";
    for (uint16_t i = 0; i < len; i++) {
      _hexBuf[i * 2]     = hex_chars[(buf[i] >> 4) & 0x0F];
      _hexBuf[i * 2 + 1] = hex_chars[buf[i] & 0x0F];
    }
    _hexBuf[len * 2] = '\0';

    if (_mqttClient.publish(mqtt_packet_topic, _hexBuf)) {
      MQTT_DEBUG_PRINTLN("PUB raw=%s", _hexBuf);
      _seen_packets.markSeen(packet);
    } else {
      MQTT_DEBUG_PRINTLN("PUB failed len=%d", len);
    }
  }
}

void MQTTBridge::onPacketReceived(mesh::Packet *packet) {
  // Zero-hop adverts (ROUTE_TYPE_DIRECT, path_len == 0) from other nodes must
  // be transmitted over radio as zero-hop so only nearby radio nodes receive
  // them. The normal queueInbound path won't do this — routeRecvPacket() only
  // retransmits flood packets. Call sendZeroHop() directly instead.
  if (_mesh
      && packet->getPayloadType() == PAYLOAD_TYPE_ADVERT
      && packet->isRouteDirect()
      && packet->path_len == 0
      && packet->payload_len >= PUB_KEY_SIZE
      && memcmp(packet->payload, _pubKey, PUB_KEY_SIZE) != 0) {
    if (!_seen_packets.wasSeen(packet)) {
      _seen_packets.markSeen(packet);
      MQTT_DEBUG_PRINTLN("SUB zero-hop advert → sendZeroHop");
      _mesh->sendZeroHop(packet);
    } else {
      _mgr->free(packet);
    }
    return;
  }
  handleReceivedPacket(packet);
}

void MQTTBridge::mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (_instance) {
    _instance->onMqttMessage(topic, payload, length);
  }
}

void MQTTBridge::onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  if (!_initialized) return;

  // Expect even number of hex chars
  if (length == 0 || length % 2 != 0) {
    MQTT_DEBUG_PRINTLN("SUB invalid hex length %u", length);
    return;
  }

  uint16_t byte_len = length / 2;
  if (byte_len > MAX_TRANS_UNIT + 1) {
    MQTT_DEBUG_PRINTLN("SUB packet too large %u bytes", byte_len);
    return;
  }

  // Decode hex into a temporary buffer
  uint8_t buf[MAX_TRANS_UNIT + 1];
  for (uint16_t i = 0; i < byte_len; i++) {
    char hi = (char)payload[i * 2];
    char lo = (char)payload[i * 2 + 1];

    uint8_t hi_val, lo_val;
    if      (hi >= '0' && hi <= '9') hi_val = hi - '0';
    else if (hi >= 'A' && hi <= 'F') hi_val = hi - 'A' + 10;
    else if (hi >= 'a' && hi <= 'f') hi_val = hi - 'a' + 10;
    else { MQTT_DEBUG_PRINTLN("SUB invalid hex char '%c'", hi); return; }

    if      (lo >= '0' && lo <= '9') lo_val = lo - '0';
    else if (lo >= 'A' && lo <= 'F') lo_val = lo - 'A' + 10;
    else if (lo >= 'a' && lo <= 'f') lo_val = lo - 'a' + 10;
    else { MQTT_DEBUG_PRINTLN("SUB invalid hex char '%c'", lo); return; }

    buf[i] = (hi_val << 4) | lo_val;
  }

  mesh::Packet *pkt = _mgr->allocNew();
  if (!pkt) {
    MQTT_DEBUG_PRINTLN("SUB alloc failed");
    return;
  }

  if (pkt->readFrom(buf, byte_len)) {
    if (_seen_packets.wasSeen(pkt)) {
      _mgr->free(pkt);
    } else {
      MQTT_DEBUG_PRINTLN("SUB raw=%.*s", (int)length, payload);
      onPacketReceived(pkt);
    }
  } else {
    MQTT_DEBUG_PRINTLN("SUB parse failed len=%u", byte_len);
    _mgr->free(pkt);
  }
}

#endif
