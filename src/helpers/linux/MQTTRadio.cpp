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
      _connected(false), _last_reconnect_ms(0),
      _n_recv(0), _n_sent(0)
{
    // defaults — overridden by env vars in target.cpp before begin()
    strncpy(mqtt_host,     "localhost",           sizeof(mqtt_host)-1);
    mqtt_port = 1883;
    strncpy(mqtt_topic,    "meshcore/packets",    sizeof(mqtt_topic)-1);
    mqtt_username[0] = '\0';
    mqtt_password[0] = '\0';
    strncpy(_client_id, "meshcore-virt",          sizeof(_client_id)-1);

    memset(_ring,        0, sizeof(_ring));
    memset(_sent_hashes, 0, sizeof(_sent_hashes));

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

    reconnect();
    mosquitto_loop_start(_mosq);
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
        mosquitto_subscribe(mosq, nullptr, self->mqtt_topic, 0);
        printf("MQTTRadio: subscribed to %s\n", self->mqtt_topic);
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

int MQTTRadio::recvRaw(uint8_t* bytes, int sz) {
    pthread_mutex_lock(&_ring_mutex);
    if (_ring_read == _ring_write) {
        pthread_mutex_unlock(&_ring_mutex);
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

    int rc = mosquitto_publish(_mosq, nullptr, mqtt_topic, len*2, hex, 0, false);
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
