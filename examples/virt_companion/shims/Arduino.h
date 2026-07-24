#pragma once
// Linux shim satisfying #include <Arduino.h> for the virtual companion build.
// Maps Arduino-named symbols directly to their Linux/stdlib equivalents.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

// ---- timing ----------------------------------------------------------------

static inline unsigned long millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

static inline unsigned long micros() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL);
}

static inline void delay(unsigned long ms) { usleep((useconds_t)(ms * 1000UL)); }
static inline void delayMicroseconds(unsigned long us) { usleep((useconds_t)us); }

// ---- random ----------------------------------------------------------------
// POSIX random() takes no args; Arduino random() takes max or (min,max).
// Use overloaded inline functions to disambiguate.

static inline void randomSeed(unsigned long seed) { srandom((unsigned int)seed); }

// Two-arg overload: random(min, max)
static inline long random(long min_val, long max_val) {
    if (max_val <= min_val) return min_val;
    return min_val + (long)((unsigned long)::random() % (unsigned long)(max_val - min_val));
}
// One-arg overload: random(max)
static inline long random(long max_val) {
    if (max_val <= 0) return 0;
    return (long)((unsigned long)::random() % (unsigned long)max_val);
}

// ---- flash / PROGMEM stubs -------------------------------------------------

#define F(s)     (s)
#define PROGMEM
#define PGM_P    const char*
#define PSTR(s)  (s)
#define pgm_read_byte(p)  (*(const uint8_t*)(p))

// ---- numeric helpers -------------------------------------------------------

#ifndef min
template<typename T> static inline T min(T a, T b) { return a < b ? a : b; }
template<typename T> static inline T max(T a, T b) { return a > b ? a : b; }
#endif
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
static inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// abs() is provided by <stdlib.h> / <cmath> on Linux; do not redefine as a
// macro — it breaks std::abs overloads inside C++ standard library headers.
#define sq(x)  ((x)*(x))

// ltoa / utoa / itoa — provided by some toolchains but not glibc
static inline char* ltoa(long val, char* buf, int base) {
    if (base == 10) { snprintf(buf, 32, "%ld", val); return buf; }
    if (base == 16) { snprintf(buf, 32, "%lx", val); return buf; }
    snprintf(buf, 32, "%ld", val); return buf;
}
static inline char* utoa(unsigned long val, char* buf, int base) {
    if (base == 10) { snprintf(buf, 32, "%lu", val); return buf; }
    if (base == 16) { snprintf(buf, 32, "%lx", val); return buf; }
    snprintf(buf, 32, "%lu", val); return buf;
}
static inline char* itoa(int val, char* buf, int base) { return ltoa((long)val, buf, base); }

#define HEX  16
#define DEC  10
#define OCT   8
#define BIN   2

// ---- Print / Stream --------------------------------------------------------

class Print {
public:
    virtual size_t write(uint8_t c) { return fwrite(&c, 1, 1, stdout); }
    virtual size_t write(const uint8_t* buf, size_t n) { return fwrite(buf, 1, n, stdout); }

    size_t print(const char* s)          { return fputs(s, stdout) >= 0 ? strlen(s) : 0; }
    size_t print(char c)                 { return write((uint8_t)c); }
    size_t print(int n, int base=10)     { return ::printf(base==16?"%x":"%d", n); }
    size_t print(unsigned n, int base=10){ return ::printf(base==16?"%x":"%u", n); }
    size_t print(long n, int base=10)    { return ::printf(base==16?"%lx":"%ld", n); }
    size_t print(unsigned long n, int b=10){ return ::printf(b==16?"%lx":"%lu", n); }
    size_t print(float f, int d=2)       { return ::printf("%.*f", d, (double)f); }
    size_t print(double f, int d=2)      { return ::printf("%.*f", d, f); }

    size_t println(const char* s)        { return ::printf("%s\n", s); }
    size_t println(char c)               { return ::printf("%c\n", c); }
    size_t println(int n, int b=10)      { return ::printf(b==16?"%x\n":"%d\n", n); }
    size_t println(unsigned long n, int b=10){ return ::printf(b==16?"%lx\n":"%lu\n", n); }
    size_t println(float f, int d=2)     { return ::printf("%.*f\n", d, (double)f); }
    size_t println()                     { return ::printf("\n"); }

    int printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int r = vprintf(fmt, ap); va_end(ap);
        return r;
    }
};

class Stream : public Print {
public:
    virtual int    available()                          { return 0; }
    virtual int    read()                               { return -1; }
    virtual int    peek()                               { return -1; }
    virtual void   flush()                              {}
    size_t readBytes(uint8_t* buf, size_t len)          { (void)buf; (void)len; return 0; }
    size_t readBytes(char*    buf, size_t len)          { (void)buf; (void)len; return 0; }
};

// Stub Arduino File type used by DataStore CLI helpers (cat/rm commands).
// Never actually opened on Linux — just needs to compile.
class File {
public:
    operator bool()  const                      { return false; }
    int    available()                          { return 0; }
    size_t read(uint8_t*, size_t)               { return 0; }
    size_t write(const uint8_t*, size_t n)      { return n; }
    size_t write(uint8_t)                       { return 1; }
    void   close()                              {}
    File   openNextFile()                       { return File(); }
    bool   isDirectory()                        { return false; }
    const char* name()                          { return ""; }
    size_t size()                               { return 0; }
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long /*baud*/) {}
    using Print::printf;
};

extern HardwareSerial Serial;

// ---- FILESYSTEM stub --------------------------------------------------------
// Arduino sketches use FILESYSTEM* (e.g. &LittleFS) for file persistence.
// On Linux we use LinuxDataStore instead; these methods are never called but
// must exist so RegionMap.cpp and CommonCLI.cpp compile.
#define FILE_O_READ  "r"
#define FILE_O_WRITE "w"

struct FILESYSTEM {
    File open(const char*, const char* = "r", bool = false) { return File(); }
    bool exists(const char*)                                 { return false; }
    bool remove(const char*)                                 { return false; }
};
