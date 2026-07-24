#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
#include <helpers/linux/LinuxTCPInterface.h>
#include "DataStore.h"
#include "target.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t _running = 1;
static void handle_signal(int) { _running = 0; }

static LinuxDataStore    store("/data");
static LinuxTCPInterface serial_interface;

StdRNG           fast_rng;
SimpleMeshTables tables;
MyMesh           the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

int main(int argc, char* argv[]) {
    _argv = argv;

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    const char* node_name = getenv("NODE_NAME");
    const char* tcp_port  = getenv("TCP_PORT");

    if (tcp_port && tcp_port[0]) serial_interface.setPort(atoi(tcp_port));

    board.begin();

    if (!radio_init()) {
        fprintf(stderr, "radio_init failed\n");
        return 1;
    }

    // Seed RNG from /dev/urandom; fall back to wall clock.
    {
        long seed = (long)time(nullptr);
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) { fread(&seed, sizeof(seed), 1, f); fclose(f); }
        fast_rng.begin(seed);
    }

    store.begin();
    the_mesh.begin(false);

    // Apply NODE_NAME env var — overrides whatever was in stored prefs.
    if (node_name && node_name[0]) {
        strncpy(the_mesh.getNodePrefs()->node_name, node_name,
                sizeof(the_mesh.getNodePrefs()->node_name) - 1);
        the_mesh.getNodePrefs()->node_name[sizeof(the_mesh.getNodePrefs()->node_name) - 1] = '\0';
        the_mesh.savePrefs();
    }

    serial_interface.begin();
    the_mesh.startInterface(serial_interface);
    sensors.begin();

    board.onBootComplete();

    while (_running) {
        the_mesh.loop();
        sensors.loop();
        rtc_clock.tick();
    }

    return 0;
}
