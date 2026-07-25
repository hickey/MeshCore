#include <Arduino.h>
#include <Mesh.h>

#include "MyMesh.h"
#include "target.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t _running = 1;
static void handle_signal(int) { _running = 0; }

StdRNG           fast_rng;
SimpleMeshTables tables;
MyMesh           the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

static char command[MAX_POST_TEXT_LEN+1];

int main(int argc, char* argv[]) {
    _argv = argv;

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    Serial.begin(115200);

    const char* node_name = getenv("NODE_NAME");

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

    FILESYSTEM fs;

    bool loaded_identity = false;
    if (fs.exists("/identity")) {
        File file = fs.open("/identity");
        if (file) {
            loaded_identity = the_mesh.self_id.readFrom(file);
            file.close();
        }
    }
    if (!loaded_identity) {
        the_mesh.self_id = radio_new_identity();   // create new random identity
        int count = 0;
        while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
            the_mesh.self_id = radio_new_identity(); count++;
        }
        File file = fs.open("/identity", "w", true);
        if (file) {
            the_mesh.self_id.writeTo(file);
            file.close();
        }
    }

    Serial.print("Room ID: ");
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

    radio_driver.setClientId("meshcore-room-server", the_mesh.self_id.pub_key, PUB_KEY_SIZE);

    // NODE_NAME env var only applies on first boot (no saved prefs yet).
    // Afterwards the persisted name always wins, so check for existing prefs
    // BEFORE the_mesh.begin() loads (and thus creates) them.
    bool is_first_boot = !fs.exists("/com_prefs") && !fs.exists("/node_prefs");

    sensors.begin();

    the_mesh.begin(&fs);

    if (is_first_boot && node_name && node_name[0]) {
        strncpy(the_mesh.getNodePrefs()->node_name, node_name,
                sizeof(the_mesh.getNodePrefs()->node_name) - 1);
        the_mesh.getNodePrefs()->node_name[sizeof(the_mesh.getNodePrefs()->node_name) - 1] = '\0';
        the_mesh.savePrefs();
    }

    command[0] = 0;

    the_mesh.sendSelfAdvertisement(16000, false);

    board.onBootComplete();

    while (_running) {
        int len = strlen(command);
        while (Serial.available() && len < (int)sizeof(command)-1) {
            char c = Serial.read();
            if (c != '\n') {
                command[len++] = c;
                command[len] = 0;
            }
            Serial.print(c);
        }
        if (len == (int)sizeof(command)-1) {  // command buffer full
            command[sizeof(command)-1] = '\r';
        }

        if (len > 0 && command[len - 1] == '\r') {  // received complete line
            command[len - 1] = 0;  // replace newline with C string null terminator
            char reply[160];
            reply[0] = 0;
            the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
            if (reply[0]) {
                Serial.print("  -> "); Serial.println(reply);
            }
            command[0] = 0;  // reset command buffer
        }

        the_mesh.loop();
        sensors.loop();
        rtc_clock.tick();
    }

    return 0;
}
