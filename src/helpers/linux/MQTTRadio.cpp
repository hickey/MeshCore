#include <helpers/linux/MQTTRadio.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// Nominal LoRa-equivalent airtime at SF10/BW250 kHz (≈ 600 bps effective).
// Used only by the mesh layer to schedule retransmissions.
static constexpr uint32_t BYTES_PER_MS = 1; // ~1 byte/ms → roughly 8 kbps raw

MQTTRadio::MQTTRadio()
    : _ring_write(0), _ring_read(0),
      _sent_write(0),
      _mosq(nullptr), _send_complete(true),
      _connected(false), _last_reconnect_ms(0), _last_status_publish_ms(0),
      _n_recv(0), _n_sent(0),
      _mesh(nullptr), _ms(nullptr), _rtc(nullptr), _tables(nullptr), _pub_key(nullptr)
{
    // defaults — overridden by env vars in target.cpp before begin()
    strncpy(mqtt_host,         "localhost",                            sizeof(mqtt_host)-1);
    mqtt_port = 1883;
    mqtt_status_interval = 60;
    strncpy(mqtt_packet_topic, "meshcore/bridge/default/packets",     sizeof(mqtt_packet_topic)-1);
    strncpy(mqtt_status_topic, "meshcore/bridge/default/status",      sizeof(mqtt_status_topic)-1);
    mqtt_username[0] = '\0';
    mqtt_password[0] = '\0';
    node_name[0] = '\0';
    strncpy(_client_id, "meshcore-virt",                              sizeof(_client_id)-1);

    memset(_ring,        0, sizeof(_ring));
    memset(_sent_hashes, 0, sizeof(_sent_hashes));
    memset(_status_buf,  0, sizeof(_status_buf));

    pthread_mutex_init(&_ring_mutex, nullptr);
    pthread_mutex_init(&_sent_mutex, nullptr);
}

MQTTRadio::~MQTTRadio() {
    if (_mosq) {
        mosquitto_loop_stop(_mosq, true);
        mosquitto_disconnect(_mosq);
        mosquitto_destroy(_mosq);
        _mosq = nullptr;
    }
    mosquitto_lib_cleanup();
    pthread_mutex_destroy(&_ring_mutex);
    pthread_mutex_destroy(&_sent_mutex);
}

void MQTTRadio::setClientId(const char* prefix, const uint8_t* pub_key, size_t len) {
    _pub_key = pub_key;
    snprintf(_client_id, sizeof(_client_id),
             "%s-%02X%02X%02X",
             prefix,
             len > 0 ? pub_key[0] : 0,
             len > 1 ? pub_key[1] : 0,
             len > 2 ? pub_key[2] : 0);
}

void MQTTRadio::begin() {
    mosquitto_lib_init();

    _mosq = mosquitto_new(_client_id, true, this);
    if (!_mosq) { fprintf(stderr, "MQTTRadio: mosquitto_new failed\n"); return; }

    mosquitto_connect_callback_set(_mosq, onConnect);
    mosquitto_message_callback_set(_mosq, onMessage);
    mosquitto_disconnect_callback_set(_mosq, onDisconnect);

    if (mqtt_username[0]) {
        mosquitto_username_pw_set(_mosq, mqtt_username,
                                  mqtt_password[0] ? mqtt_password : nullptr);
    }

    // Set up LWT (Last Will and Testament) for offline status
    if (_pub_key) {
        char statusTopic[128];
        char pubkeyHex[64 * 2 + 1];
        for (uint8_t i = 0; i < 32; i++) {
            snprintf(&pubkeyHex[i * 2], 3, "%02X", _pub_key[i]);
        }
        snprintf(statusTopic, sizeof(statusTopic), "%s/%s", mqtt_status_topic, pubkeyHex);
        const char *offlineMsg = "{\"status\":\"offline\"}";
        mosquitto_will_set(_mosq, statusTopic, strlen(offlineMsg), offlineMsg, 0, true);
    }

    reconnect();
    mosquitto_loop_start(_mosq);
    _last_status_publish_ms = time(nullptr) * 1000;
}

bool MQTTRadio::reconnect() {
    if (!_mosq) return false;
    int rc = mosquitto_connect(_mosq, mqtt_host, mqtt_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTTRadio: connect to %s:%d failed: %s\n",
                mqtt_host, mqtt_port, mosquitto_strerror(rc));
        return false;
    }
    return true;
}

// ---- static callbacks ------------------------------------------------------

void MQTTRadio::onConnect(struct mosquitto* mosq, void* userdata, int rc) {
    auto* self = static_cast<MQTTRadio*>(userdata);
    if (rc == 0) {
        self->_connected = true;
        printf("MQTTRadio: connected to %s:%d\n", self->mqtt_host, self->mqtt_port);
        mosquitto_subscribe(mosq, nullptr, self->mqtt_packet_topic, 0);
        printf("MQTTRadio: subscribed to %s\n", self->mqtt_packet_topic);

        // Publish initial status
        if (self->_pub_key) {
            char statusTopic[128];
            char pubkeyHex[64 * 2 + 1];
            for (uint8_t i = 0; i < 32; i++) {
                snprintf(&pubkeyHex[i * 2], 3, "%02X", self->_pub_key[i]);
            }
            snprintf(statusTopic, sizeof(statusTopic), "%s/%s", self->mqtt_status_topic, pubkeyHex);
            const char* status_msg = self->buildStatusMessage("online");
            mosquitto_publish(mosq, nullptr, statusTopic, strlen(status_msg), status_msg, 0, true);
            printf("MQTTRadio: published status to %s\n", statusTopic);
        }
    } else {
        fprintf(stderr, "MQTTRadio: connect failed rc=%d\n", rc);
    }
}

void MQTTRadio::onDisconnect(struct mosquitto*, void* userdata, int rc) {
    auto* self = static_cast<MQTTRadio*>(userdata);
    self->_connected = false;
    if (rc != 0)
        fprintf(stderr, "MQTTRadio: unexpected disconnect rc=%d, will reconnect\n", rc);
}

void MQTTRadio::onMessage(struct mosquitto*, void* userdata,
                           const struct mosquitto_message* msg)
{
    if (!msg || !msg->payload || msg->payloadlen <= 0) return;
    if (msg->payloadlen % 2 != 0) return; // must be even hex

    auto* self = static_cast<MQTTRadio*>(userdata);

    int byte_len = msg->payloadlen / 2;
    if (byte_len > MQTTRadio::MAX_RAW) return;

    uint8_t buf[MAX_RAW];
    const char* hex = static_cast<const char*>(msg->payload);

    for (int i = 0; i < byte_len; i++) {
        auto hv = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hi = hv(hex[i*2]);
        int lo = hv(hex[i*2+1]);
        if (hi < 0 || lo < 0) return;
        buf[i] = (uint8_t)((hi << 4) | lo);
    }

    // Drop own echoes
    if (self->wasSentByUs(buf, byte_len)) return;

    self->pushInbound(buf, byte_len);
}

// ---- inbound ring buffer ---------------------------------------------------

void MQTTRadio::pushInbound(const uint8_t* data, int len) {
    pthread_mutex_lock(&_ring_mutex);
    int next = (_ring_write + 1) % RING_SIZE;
    if (next != _ring_read) {
        memcpy(_ring[_ring_write].data, data, len);
        _ring[_ring_write].len = len;
        _ring_write = next;
        _n_recv++;
    }
    pthread_mutex_unlock(&_ring_mutex);
}

// Build a JSON status message with node statistics
const char* MQTTRadio::buildStatusMessage(const char *status) {
    int offset = snprintf(_status_buf, STATUS_BUF_SIZE, "{\"status\":\"%s\"", status);

    // Add model
    offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"model\":\"Virtual Room Server\"");

    // Add node name if available
    if (node_name[0]) {
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"node_name\":\"%s\"", node_name);
    }

    // Add uptime if millisecond clock available
    if (_ms) {
        uint32_t uptime_secs = (uint32_t)(_ms->getMillis() / 1000);
        char uptime_str[32];
        uint32_t mins = (uptime_secs / 60) % 60;
        uint32_t hours = (uptime_secs / 3600) % 24;
        uint32_t days = uptime_secs / 86400;

        snprintf(uptime_str, sizeof(uptime_str), "%ud%uh%um", days, hours, mins);
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ", \"uptime\": \"%s\"", uptime_str);
    }

    // Add RTC clock timestamp if available
    if (_rtc) {
        uint32_t timestamp = _rtc->getCurrentTime();
        time_t t = (time_t)timestamp;
        struct tm timeinfo;
        gmtime_r(&t, &timeinfo);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ", \"clock\": \"%s\"", timeBuf);
    }

    // Add mesh statistics if available
    if (_mesh) {
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"tx_packets\":%u", _mesh->getNumSentFlood() + _mesh->getNumSentDirect());
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"tx_flood\":%u", _mesh->getNumSentFlood());
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"tx_direct\":%u", _mesh->getNumSentDirect());
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"rx_packets\":%u", _mesh->getNumRecvFlood() + _mesh->getNumRecvDirect());
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"rx_flood\":%u", _mesh->getNumRecvFlood());
        offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"rx_direct\":%u", _mesh->getNumRecvDirect());

        // Add duplicate packet counts if SimpleMeshTables available
        if (_tables) {
            offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"dup_flood\":%u", _tables->getNumFloodDups());
            offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"dup_direct\":%u", _tables->getNumDirectDups());
        }
    }

    // Add radio driver statistics (errors only for now)
    offset += snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, ",\"rx_errors\":%u", getPacketsRecvErrors());


    snprintf(_status_buf + offset, STATUS_BUF_SIZE - offset, "}");
    return _status_buf;
}

int MQTTRadio::recvRaw(uint8_t* bytes, int sz) {
    pthread_mutex_lock(&_ring_mutex);
    if (_ring_read == _ring_write) {
        pthread_mutex_unlock(&_ring_mutex);

        // Publish periodic status updates
        if (_connected && _pub_key) {
            unsigned long now_ms = time(nullptr) * 1000;
            if (now_ms - _last_status_publish_ms >= (mqtt_status_interval * 1000)) {
                _last_status_publish_ms = now_ms;

                char statusTopic[128];
                char pubkeyHex[64 * 2 + 1];
                for (uint8_t i = 0; i < 32; i++) {
                    snprintf(&pubkeyHex[i * 2], 3, "%02X", _pub_key[i]);
                }
                snprintf(statusTopic, sizeof(statusTopic), "%s/%s", mqtt_status_topic, pubkeyHex);
                const char* status_msg = buildStatusMessage("online");
                mosquitto_publish(_mosq, nullptr, statusTopic, strlen(status_msg), status_msg, 0, true);
            }
        }

        return 0;
    }
    int len = _ring[_ring_read].len;
    if (len <= sz) memcpy(bytes, _ring[_ring_read].data, len);
    _ring_read = (_ring_read + 1) % RING_SIZE;
    pthread_mutex_unlock(&_ring_mutex);
    return (len <= sz) ? len : 0;
}

// ---- send ------------------------------------------------------------------

uint32_t MQTTRadio::fnv1a(const uint8_t* data, int len) {
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < len; i++) { h ^= data[i]; h *= 0x01000193u; }
    return h;
}

void MQTTRadio::markSent(const uint8_t* data, int len) {
    uint32_t h = fnv1a(data, len);
    pthread_mutex_lock(&_sent_mutex);
    _sent_hashes[_sent_write] = h;
    _sent_write = (_sent_write + 1) % SENT_RING;
    pthread_mutex_unlock(&_sent_mutex);
}

bool MQTTRadio::wasSentByUs(const uint8_t* data, int len) const {
    uint32_t h = fnv1a(data, len);
    pthread_mutex_lock(const_cast<pthread_mutex_t*>(&_sent_mutex));
    bool found = false;
    for (int i = 0; i < SENT_RING; i++) {
        if (_sent_hashes[i] == h) { found = true; break; }
    }
    pthread_mutex_unlock(const_cast<pthread_mutex_t*>(&_sent_mutex));
    return found;
}

bool MQTTRadio::startSendRaw(const uint8_t* bytes, int len) {
    _send_complete = false;

    if (!_mosq || !_connected) {
        _send_complete = true;
        return false;
    }

    // Hex-encode as uppercase ASCII
    static const char* H = "0123456789ABCDEF";
    static constexpr int MAX_HEX = (MAX_RAW + 1) * 2 + 1;
    char hex[MAX_HEX];
    for (int i = 0; i < len; i++) {
        hex[i*2]   = H[(bytes[i] >> 4) & 0xF];
        hex[i*2+1] = H[bytes[i] & 0xF];
    }
    hex[len*2] = '\0';

    markSent(bytes, len);

    int rc = mosquitto_publish(_mosq, nullptr, mqtt_packet_topic, len*2, hex, 0, false);
    if (rc == MOSQ_ERR_SUCCESS) _n_sent++;
    _send_complete = true;
    return rc == MOSQ_ERR_SUCCESS;
}

bool MQTTRadio::isSendComplete() { return _send_complete; }

// ---- misc mesh::Radio ------------------------------------------------------

uint32_t MQTTRadio::getEstAirtimeFor(int len_bytes) {
    // Return a plausible LoRa-like airtime so the mesh scheduler behaves
    // correctly.  At SF10/BW250 the raw symbol rate is ~244 baud; a 100-byte
    // packet takes roughly 400 ms.  We scale linearly from that baseline.
    return (uint32_t)(50 + len_bytes * 3);
}

float MQTTRadio::packetScore(float /*snr*/, int /*packet_len*/) {
    return 1.0f; // perfect reception — no fading on an MQTT broker
}
