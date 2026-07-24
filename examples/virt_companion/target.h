#pragma once

#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/linux/MQTTRadio.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ---- Linux board -----------------------------------------------------------

class LinuxBoard {
public:
    void   begin()           {}
    void   onBootComplete()  {}
    void   sleep(int)        {}
    void   setInhibitSleep(bool) {}

    uint16_t    getBattMilliVolts() const  { return 0; }
    float       getMCUTemperature() const  { return 0.0f; }
    const char* getManufacturerName() const { return "VirtualCompanion"; }

    // Re-exec the process so Docker's restart policy kicks in.
    void reboot() {
        extern char** _argv;
        fflush(nullptr);
        execv("/proc/self/exe", _argv);
        // fallback if /proc/self/exe is unavailable
        exit(0);
    }
};

// ---- Linux RTC clock -------------------------------------------------------
// Uses the system wall clock for getCurrentTime() and millis()-based VolatileRTC
// tick.  Initialised with real wall-clock time so MeshCore timestamps are sane.

class LinuxRTCClock : public mesh::RTCClock {
    uint32_t _base;
    unsigned long _base_millis;
public:
    LinuxRTCClock() {
        _base       = (uint32_t)time(nullptr);
        _base_millis = millis();
    }
    uint32_t getCurrentTime() override {
        return _base + (uint32_t)((millis() - _base_millis) / 1000UL);
    }
    void setCurrentTime(uint32_t t) override {
        _base        = t;
        _base_millis = millis();
    }
    void tick() override {}  // no accumulator needed — getCurrentTime reads millis directly
};

// ---- globals declared here, defined in target.cpp -------------------------

extern LinuxBoard        board;
extern MQTTRadio         radio_driver;
extern LinuxRTCClock     rtc_clock;
extern SensorManager     sensors;
extern char**            _argv;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
