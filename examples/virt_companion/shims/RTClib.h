#pragma once
// Minimal RTClib shim for the virtual companion Linux build.
// Only DateTime is used (in BridgeBase logging); we provide that here.

#include <stdint.h>
#include <time.h>

class DateTime {
    uint32_t _unixtime;
public:
    explicit DateTime(uint32_t t = 0) : _unixtime(t) {}

    uint32_t unixtime() const { return _unixtime; }

    uint16_t year() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint16_t)(tm->tm_year + 1900);
    }
    uint8_t month() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint8_t)(tm->tm_mon + 1);
    }
    uint8_t day() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint8_t)tm->tm_mday;
    }
    uint8_t hour() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint8_t)tm->tm_hour;
    }
    uint8_t minute() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint8_t)tm->tm_min;
    }
    uint8_t second() const {
        time_t t = (time_t)_unixtime;
        struct tm* tm = gmtime(&t);
        return (uint8_t)tm->tm_sec;
    }
};
