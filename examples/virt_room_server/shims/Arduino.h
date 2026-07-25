#pragma once
// Linux shim satisfying #include <Arduino.h> for the virtual room server build.
// Maps Arduino-named symbols directly to their Linux/stdlib equivalents.
//
// Unlike virt_companion's shim, FILESYSTEM/File here are REAL POSIX-backed
// implementations (not stubs) — CommonCLI.cpp, RegionMap.cpp, ClientACL.cpp,
// and IdentityStore.cpp all persist state through FILESYSTEM*/File, and are
// compiled unmodified in this build. Every path is rooted at a fixed base
// directory (set via file_shim_set_base_dir(), defaults to "/data") with no
// subdirectories created.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

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
template<typename T1, typename T2> static inline auto min(T1 a, T2 b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template<typename T1, typename T2> static inline auto max(T1 a, T2 b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
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
    virtual size_t readBytes(uint8_t* buf, size_t len)  { (void)buf; (void)len; return 0; }
    size_t readBytes(char* buf, size_t len)             { return readBytes((uint8_t*)buf, len); }
};

// ---- File / FILESYSTEM ------------------------------------------------------
// Real POSIX-backed implementations. All the room-server persistence helpers
// (CommonCLI.cpp, RegionMap.cpp, ClientACL.cpp, IdentityStore.cpp) are
// compiled unmodified against FILESYSTEM*/File, so these need to actually
// read/write files rather than stub out as no-ops.
//
// Every relative path is rooted under a single fixed base directory (set via
// file_shim_set_base_dir(), default "/data") — no subdirectories are ever
// created, matching the flat /data layout used elsewhere in this project.

inline char g_file_shim_base_dir[256] = "/data";

static inline void file_shim_set_base_dir(const char* dir) {
    strncpy(g_file_shim_base_dir, dir, sizeof(g_file_shim_base_dir) - 1);
    g_file_shim_base_dir[sizeof(g_file_shim_base_dir) - 1] = '\0';
    size_t n = strlen(g_file_shim_base_dir);
    if (n > 1 && g_file_shim_base_dir[n - 1] == '/') g_file_shim_base_dir[n - 1] = '\0';
}
static inline const char* file_shim_get_base_dir() { return g_file_shim_base_dir; }

class File : public Stream {
    FILE* _fp = nullptr;
    char  _name[64] = {};
public:
    File() {}
    explicit File(FILE* fp, const char* name) : _fp(fp) {
        strncpy(_name, name, sizeof(_name) - 1);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& o) noexcept : _fp(o._fp) {
        strncpy(_name, o._name, sizeof(_name) - 1);
        o._fp = nullptr;
    }
    File& operator=(File&& o) noexcept {
        if (this != &o) {
            if (_fp) fclose(_fp);
            _fp = o._fp;
            strncpy(_name, o._name, sizeof(_name) - 1);
            o._fp = nullptr;
        }
        return *this;
    }
    ~File() { if (_fp) fclose(_fp); }

    operator bool() const { return _fp != nullptr; }

    int available() override {
        if (!_fp) return 0;
        long cur = ftell(_fp);
        if (cur < 0) return 0;
        fseek(_fp, 0, SEEK_END);
        long end = ftell(_fp);
        fseek(_fp, cur, SEEK_SET);
        return (int)(end - cur);
    }
    int read() override {
        if (!_fp) return -1;
        int c = fgetc(_fp);
        return c == EOF ? -1 : c;
    }
    size_t read(uint8_t* buf, size_t len) {
        if (!_fp) return 0;
        return fread(buf, 1, len, _fp);
    }
    size_t readBytes(uint8_t* buf, size_t len) override { return read(buf, len); }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t len) override {
        if (!_fp) return 0;
        return fwrite(buf, 1, len, _fp);
    }
    void close() { if (_fp) { fclose(_fp); _fp = nullptr; } }
    const char* name() { return _name; }
    size_t size() {
        if (!_fp) return 0;
        long cur = ftell(_fp);
        fseek(_fp, 0, SEEK_END);
        long end = ftell(_fp);
        fseek(_fp, cur, SEEK_SET);
        return (size_t)end;
    }
    // Directory iteration is never used by simple_room_server; kept only so
    // any shared helper code referencing it still compiles.
    File openNextFile()  { return File(); }
    bool isDirectory()   { return false; }
};

#define FILE_O_READ  "r"
#define FILE_O_WRITE "w"

struct FILESYSTEM {
    static void fullPath(char* out, size_t sz, const char* rel) {
        snprintf(out, sz, "%s%s", file_shim_get_base_dir(), rel);
    }

    File open(const char* rel, const char* mode = "r", bool /*create*/ = false) {
        char path[512]; fullPath(path, sizeof(path), rel);
        FILE* fp = fopen(path, mode);
        if (!fp) return File();
        return File(fp, rel);
    }
    bool exists(const char* rel) {
        char path[512]; fullPath(path, sizeof(path), rel);
        return access(path, F_OK) == 0;
    }
    bool remove(const char* rel) {
        char path[512]; fullPath(path, sizeof(path), rel);
        return ::remove(path) == 0;
    }
    bool mkdir(const char*) { return true; } // no-op: flat /data layout, no subdirectories
};

// ---- HardwareSerial ---------------------------------------------------------
// Backed by real stdin/stdout so the admin CLI works via `docker attach` /
// an interactive `docker run -it`. stdin is put in non-blocking mode so the
// main loop (mesh processing + CLI polling) never stalls waiting on input.

class HardwareSerial : public Stream {
    bool _eof = false;
public:
    void begin(unsigned long /*baud*/) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    int available() override {
        if (_eof) return 0;
        struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
        return poll(&pfd, 1, 0) > 0 ? 1 : 0;
    }
    int read() override {
        if (_eof) return -1;
        uint8_t c;
        ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n == 0) { _eof = true; return -1; }   // stdin closed
        return n == 1 ? (int)c : -1;
    }
    using Print::printf;
};

extern HardwareSerial Serial;
