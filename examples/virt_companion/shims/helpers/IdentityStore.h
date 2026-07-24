#pragma once
#include <Identity.h>

// Stub — identity is managed by LinuxDataStore on this platform.
class IdentityStore {
public:
    void begin() {}
    bool load(const char*, mesh::LocalIdentity&) { return false; }
    bool load(const char*, mesh::LocalIdentity&, char[], int) { return false; }
    bool save(const char*, const mesh::LocalIdentity&) { return false; }
    bool save(const char*, const mesh::LocalIdentity&, const char[]) { return false; }
};
