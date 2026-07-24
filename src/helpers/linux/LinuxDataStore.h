#pragma once

#include <stdint.h>
#include <Identity.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

struct NodePrefs;

class DataStoreHost {
public:
    virtual bool onContactLoaded(const ContactInfo& contact) = 0;
    virtual bool getContactForSave(uint32_t idx, ContactInfo& contact) = 0;
    virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) = 0;
    virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) = 0;
};

// POSIX-based DataStore for the Linux virtual companion.
// Stores all data directly under a configurable base directory (e.g. /data).
// File layout mirrors the Arduino companion_radio layout so identity/contacts
// files are interchangeable between platforms.
class LinuxDataStore {
    char _base[256];

    void   fullPath(char* out, size_t sz, const char* rel) const;
    void   ensureDir(const char* rel) const;
    bool   rmrf(const char* path) const;
    bool   writeRaw(const char* rel, const void* data, size_t len) const;
    size_t readRaw(const char* rel, void* data, size_t max_len) const;
    bool   fileExists(const char* rel) const;

public:
    explicit LinuxDataStore(const char* base_dir);

    void setBaseDir(const char* dir);

    void begin();
    bool formatFileSystem();

    bool loadMainIdentity(mesh::LocalIdentity& identity);
    bool saveMainIdentity(const mesh::LocalIdentity& identity);

    void loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon);
    void savePrefs(const NodePrefs& prefs, double node_lat, double node_lon);

    void loadContacts(DataStoreHost* host);
    void saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c) = nullptr);

    void loadChannels(DataStoreHost* host);
    void saveChannels(DataStoreHost* host);

    uint8_t getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]);
    bool    putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len);
    bool    deleteBlobByKey(const uint8_t key[], int key_len);

    uint32_t getStorageUsedKb() const;
    uint32_t getStorageTotalKb() const;

    // Stubs for Arduino filesystem CLI helpers (cat/rm commands in MyMesh).
    // These are never meaningfully invoked on Linux.
    void* getSecondaryFS() const { return nullptr; }
    File  openRead(const char* path)               { (void)path; return File(); }
    File  openRead(void* /*fs*/, const char* path) { (void)path; return File(); }
    bool  removeFile(const char* path)             { char buf[512]; fullPath(buf, sizeof(buf), path); return ::remove(buf) == 0; }
    bool  removeFile(void* /*fs*/, const char*)    { return false; }
};
