#pragma once

#include <Dispatcher.h>
#include <helpers/SimpleMeshTables.h>
#include <mosquitto.h>
#include <pthread.h>
#include <stdint.h>

// mesh::Radio implementation that sends and receives MeshCore packets over
// MQTT.  Packets are encoded as uppercase ASCII hex strings on a single shared
// topic (default "meshcore/packets"), matching the MQTTBridge wire format on
// the feat/mqtt-bridge branch.
//
// Mosquitto's network loop runs in a background thread via
// mosquitto_loop_start().  Incoming messages are decoded and placed in a
// fixed-size ring buffer; recvRaw() drains one entry per call.
//
// Own-packet suppression: every payload we publish is hashed; when the broker
// echoes it back via the subscription we drop it.
class MQTTRadio : public mesh::Radio {
public:
    MQTTRadio();
    ~MQTTRadio();

    // Call before the mesh loop starts.
    void begin() override;

    // mesh::Radio interface
    int      recvRaw(uint8_t* bytes, int sz) override;
    uint32_t getEstAirtimeFor(int len_bytes) override;
    float    packetScore(float snr, int packet_len) override;
    bool     startSendRaw(const uint8_t* bytes, int len) override;
    bool     isSendComplete() override;
    void     onSendFinished() override {}
    bool     isInRecvMode() const override { return true; }

    // Radio param stubs — silently accepted, MQTT has no physical radio params.
    void     setParams(float, float, uint8_t, uint8_t) {}
    void     setTxPower(int8_t)                        {}
    bool     setRxBoostedGainMode(bool)                { return false; }

    // Packet counters — tracked in startSendRaw / pushInbound.
    uint32_t getPacketsRecv()       const { return _n_recv; }
    uint32_t getPacketsSent()       const { return _n_sent; }
    uint32_t getPacketsRecvErrors() const { return 0; }

    // Configuration — set before begin(), or pick up from env vars.
    char mqtt_host[128];
    int  mqtt_port;
    char mqtt_topic[128];
    char mqtt_username[64];
    char mqtt_password[64];

    // Unique client ID derived from the node's public key prefix.
    // Call after identity is loaded.
    void setClientId(const uint8_t* pub_key, size_t len);

private:
    static constexpr int RING_SIZE  = 32;
    static constexpr int MAX_RAW    = 256; // MAX_TRANS_UNIT + 1 headroom

    // Inbound ring buffer (written by mosquitto callback, read by recvRaw)
    struct RawPacket { uint8_t data[MAX_RAW]; int len; };
    RawPacket  _ring[RING_SIZE];
    int        _ring_write;
    int        _ring_read;
    pthread_mutex_t _ring_mutex;

    // Sent-packet hash ring for own-echo suppression
    static constexpr int SENT_RING = 32;
    uint32_t _sent_hashes[SENT_RING];
    int      _sent_write;
    pthread_mutex_t _sent_mutex;

    struct mosquitto* _mosq;
    bool _send_complete;
    char _client_id[48];
    bool _connected;
    unsigned long _last_reconnect_ms;
    uint32_t _n_recv;
    uint32_t _n_sent;

    static void onConnect(struct mosquitto*, void*, int rc);
    static void onMessage(struct mosquitto*, void*, const struct mosquitto_message*);
    static void onDisconnect(struct mosquitto*, void*, int rc);

    bool reconnect();
    void pushInbound(const uint8_t* data, int len);
    bool wasSentByUs(const uint8_t* data, int len) const;
    void markSent(const uint8_t* data, int len);
    static uint32_t fnv1a(const uint8_t* data, int len);
};
