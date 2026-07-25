#include "target.h"
#include <Mesh.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

HardwareSerial Serial;

LinuxBoard    board;
MQTTRadio     radio_driver;
LinuxRTCClock rtc_clock;
SensorManager sensors;
char**        _argv = nullptr;

bool radio_init() {
    const char* host     = getenv("MQTT_HOST");
    const char* port_str = getenv("MQTT_PORT");
    const char* topic    = getenv("MQTT_TOPIC");
    const char* user     = getenv("MQTT_USERNAME");
    const char* pass     = getenv("MQTT_PASSWORD");

    if (host)     strncpy(radio_driver.mqtt_host,     host,     sizeof(radio_driver.mqtt_host)-1);
    if (port_str) radio_driver.mqtt_port = atoi(port_str);
    if (topic)    strncpy(radio_driver.mqtt_topic,    topic,    sizeof(radio_driver.mqtt_topic)-1);
    if (user)     strncpy(radio_driver.mqtt_username, user,     sizeof(radio_driver.mqtt_username)-1);
    if (pass)     strncpy(radio_driver.mqtt_password, pass,     sizeof(radio_driver.mqtt_password)-1);

    const char* lat_str = getenv("LOCATION_LAT");
    const char* lon_str = getenv("LOCATION_LONG");
    if (lat_str) sensors.node_lat = atof(lat_str);
    if (lon_str) sensors.node_lon = atof(lon_str);

    return true;
}

// Generate a fresh random identity using the already-seeded fast_rng.
mesh::LocalIdentity radio_new_identity() {
    extern StdRNG fast_rng;
    return mesh::LocalIdentity(&fast_rng);
}
