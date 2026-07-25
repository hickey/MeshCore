#pragma once
// Minimal CayenneLPP shim for the virtual companion Linux build.
// The virtual companion has no sensors; all add* methods are no-ops.
// The buffer/size accessors are needed for the telemetry command response path.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

class CayenneLPP {
    enum { MAX_SIZE = 64 };
    uint8_t _buf[MAX_SIZE];
    uint8_t _sz;
public:
    explicit CayenneLPP(uint8_t /*size*/ = (uint8_t)MAX_SIZE) : _sz(0) {
        memset(_buf, 0, sizeof(_buf));
    }

    void    reset()                   { _sz = 0; }
    uint8_t getSize()   const         { return (uint8_t)_sz; }
    const uint8_t* getBuffer() const  { return _buf; }

    // sensor add methods — no-ops on Linux
    bool addVoltage(uint8_t, float)          { return false; }
    bool addTemperature(uint8_t, float)      { return false; }
    bool addRelativeHumidity(uint8_t, float) { return false; }
    bool addBarometricPressure(uint8_t, float){ return false; }
    bool addGPS(uint8_t, float, float, float){ return false; }
    bool addDigitalInput(uint8_t, uint32_t)  { return false; }
    bool addAnalogInput(uint8_t, float)      { return false; }
    bool addGenericSensor(uint8_t, float)    { return false; }
};
