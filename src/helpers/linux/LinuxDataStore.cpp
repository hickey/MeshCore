#include <helpers/linux/LinuxDataStore.h>

#include <Identity.h>
#include <Utils.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <ftw.h>

#include "NodePrefs.h"

// ---- contacts record size (matches companion_radio DataStore.cpp) -----------
//   32 pub_key + 32 name + 1 type + 1 flags + 1 unused + 4 sync_since
//   + 1 out_path_len + 4 last_advert_timestamp + 64 out_path
//   + 4 lastmod + 4 gps_lat + 4 gps_lon  = 152 bytes
static constexpr size_t CONTACT_REC = 152;

static int _rmrf_cb(const char* fpath, const struct stat*, int, struct FTW*) {
    return remove(fpath);
}

static void blobPath(char* out, size_t sz, const char* base, const uint8_t key[], int key_len) {
    char hex[18] = {};
    int n = key_len < 8 ? key_len : 8;
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        hex[i*2]   = H[(key[i] >> 4) & 0xF];
        hex[i*2+1] = H[key[i] & 0xF];
    }
    snprintf(out, sz, "%s/bl/%s", base, hex);
}

// ---- LinuxDataStore --------------------------------------------------------

LinuxDataStore::LinuxDataStore(const char* base_dir) {
    strncpy(_base, base_dir, sizeof(_base) - 1);
    _base[sizeof(_base) - 1] = '\0';
    // strip trailing slash
    size_t n = strlen(_base);
    if (n > 1 && _base[n-1] == '/') _base[n-1] = '\0';
}

void LinuxDataStore::setBaseDir(const char* dir) {
    strncpy(_base, dir, sizeof(_base) - 1);
    _base[sizeof(_base) - 1] = '\0';
    size_t n = strlen(_base);
    if (n > 1 && _base[n-1] == '/') _base[n-1] = '\0';
}

void LinuxDataStore::fullPath(char* out, size_t sz, const char* rel) const {
    snprintf(out, sz, "%s%s", _base, rel);
}

void LinuxDataStore::ensureDir(const char* rel) const {
    char path[512]; fullPath(path, sizeof(path), rel);
    mkdir(path, 0755);
}

bool LinuxDataStore::rmrf(const char* path) const {
    return nftw(path, _rmrf_cb, 64, FTW_DEPTH | FTW_PHYS) == 0;
}

bool LinuxDataStore::writeRaw(const char* rel, const void* data, size_t len) const {
    char path[512]; fullPath(path, sizeof(path), rel);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

size_t LinuxDataStore::readRaw(const char* rel, void* data, size_t max_len) const {
    char path[512]; fullPath(path, sizeof(path), rel);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(data, 1, max_len, f);
    fclose(f);
    return n;
}

bool LinuxDataStore::fileExists(const char* rel) const {
    char path[512]; fullPath(path, sizeof(path), rel);
    return access(path, F_OK) == 0;
}

void LinuxDataStore::begin() {
    mkdir(_base, 0755);
    ensureDir("/bl");
}

bool LinuxDataStore::formatFileSystem() {
    return rmrf(_base);
}

// ---- identity --------------------------------------------------------------

bool LinuxDataStore::loadMainIdentity(mesh::LocalIdentity& identity) {
    uint8_t buf[200];
    size_t n = readRaw("/identity", buf, sizeof(buf));
    if (n == 0) return false;
    identity.readFrom(buf, n);
    return true;
}

bool LinuxDataStore::saveMainIdentity(const mesh::LocalIdentity& identity) {
    uint8_t buf[200];
    size_t n = const_cast<mesh::LocalIdentity&>(identity).writeTo(buf, sizeof(buf));
    if (n == 0) return false;
    return writeRaw("/identity", buf, n);
}

// ---- prefs -----------------------------------------------------------------

void LinuxDataStore::loadPrefs(NodePrefs& p, double& node_lat, double& node_lon) {
    if (!fileExists("/new_prefs")) return;
    char path[512]; fullPath(path, sizeof(path), "/new_prefs");
    FILE* f = fopen(path, "rb");
    if (!f) return;

    uint8_t pad[8];
    fread(&p.airtime_factor,        sizeof(float),         1, f);
    fread(p.node_name,              sizeof(p.node_name),   1, f);
    fread(pad,                      4,                     1, f);
    fread(&node_lat,                sizeof(node_lat),      1, f);
    fread(&node_lon,                sizeof(node_lon),      1, f);
    fread(&p.freq,                  sizeof(p.freq),        1, f);
    fread(&p.sf,                    sizeof(p.sf),          1, f);
    fread(&p.cr,                    sizeof(p.cr),          1, f);
    fread(&p.client_repeat,         sizeof(p.client_repeat),        1, f);
    fread(&p.manual_add_contacts,   sizeof(p.manual_add_contacts),  1, f);
    fread(&p.bw,                    sizeof(p.bw),          1, f);
    fread(&p.tx_power_dbm,          sizeof(p.tx_power_dbm),         1, f);
    fread(&p.telemetry_mode_base,   sizeof(p.telemetry_mode_base),  1, f);
    fread(&p.telemetry_mode_loc,    sizeof(p.telemetry_mode_loc),   1, f);
    fread(&p.telemetry_mode_env,    sizeof(p.telemetry_mode_env),   1, f);
    fread(&p.rx_delay_base,         sizeof(p.rx_delay_base),        1, f);
    fread(&p.advert_loc_policy,     sizeof(p.advert_loc_policy),    1, f);
    fread(&p.multi_acks,            sizeof(p.multi_acks),           1, f);
    fread(&p.path_hash_mode,        sizeof(p.path_hash_mode),       1, f);
    fread(pad,                      1,                     1, f);
    fread(&p.ble_pin,               sizeof(p.ble_pin),     1, f);
    fread(&p.buzzer_quiet,          sizeof(p.buzzer_quiet),         1, f);
    fread(&p.gps_enabled,           sizeof(p.gps_enabled),          1, f);
    fread(&p.gps_interval,          sizeof(p.gps_interval),         1, f);
    fread(&p.autoadd_config,        sizeof(p.autoadd_config),       1, f);
    fread(&p.autoadd_max_hops,      sizeof(p.autoadd_max_hops),     1, f);
    fread(&p.rx_boosted_gain,       sizeof(p.rx_boosted_gain),      1, f);
    fread(p.default_scope_name,     sizeof(p.default_scope_name),   1, f);
    fread(p.default_scope_key,      sizeof(p.default_scope_key),    1, f);
    fclose(f);
}

void LinuxDataStore::savePrefs(const NodePrefs& p, double node_lat, double node_lon) {
    char path[512]; fullPath(path, sizeof(path), "/new_prefs");
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint8_t pad[8] = {};
    fwrite(&p.airtime_factor,       sizeof(float),         1, f);
    fwrite(p.node_name,             sizeof(p.node_name),   1, f);
    fwrite(pad,                     4,                     1, f);
    fwrite(&node_lat,               sizeof(node_lat),      1, f);
    fwrite(&node_lon,               sizeof(node_lon),      1, f);
    fwrite(&p.freq,                 sizeof(p.freq),        1, f);
    fwrite(&p.sf,                   sizeof(p.sf),          1, f);
    fwrite(&p.cr,                   sizeof(p.cr),          1, f);
    fwrite(&p.client_repeat,        sizeof(p.client_repeat),        1, f);
    fwrite(&p.manual_add_contacts,  sizeof(p.manual_add_contacts),  1, f);
    fwrite(&p.bw,                   sizeof(p.bw),          1, f);
    fwrite(&p.tx_power_dbm,         sizeof(p.tx_power_dbm),         1, f);
    fwrite(&p.telemetry_mode_base,  sizeof(p.telemetry_mode_base),  1, f);
    fwrite(&p.telemetry_mode_loc,   sizeof(p.telemetry_mode_loc),   1, f);
    fwrite(&p.telemetry_mode_env,   sizeof(p.telemetry_mode_env),   1, f);
    fwrite(&p.rx_delay_base,        sizeof(p.rx_delay_base),        1, f);
    fwrite(&p.advert_loc_policy,    sizeof(p.advert_loc_policy),    1, f);
    fwrite(&p.multi_acks,           sizeof(p.multi_acks),           1, f);
    fwrite(&p.path_hash_mode,       sizeof(p.path_hash_mode),       1, f);
    fwrite(pad,                     1,                     1, f);
    fwrite(&p.ble_pin,              sizeof(p.ble_pin),     1, f);
    fwrite(&p.buzzer_quiet,         sizeof(p.buzzer_quiet),         1, f);
    fwrite(&p.gps_enabled,          sizeof(p.gps_enabled),          1, f);
    fwrite(&p.gps_interval,         sizeof(p.gps_interval),         1, f);
    fwrite(&p.autoadd_config,       sizeof(p.autoadd_config),       1, f);
    fwrite(&p.autoadd_max_hops,     sizeof(p.autoadd_max_hops),     1, f);
    fwrite(&p.rx_boosted_gain,      sizeof(p.rx_boosted_gain),      1, f);
    fwrite(p.default_scope_name,    sizeof(p.default_scope_name),   1, f);
    fwrite(p.default_scope_key,     sizeof(p.default_scope_key),    1, f);
    fclose(f);
}

// ---- contacts --------------------------------------------------------------

void LinuxDataStore::loadContacts(DataStoreHost* host) {
    char path[512]; fullPath(path, sizeof(path), "/contacts3");
    FILE* f = fopen(path, "rb");
    if (!f) return;

    bool full = false;
    while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool ok = fread(pub_key, 32, 1, f) == 1;
        ok = ok && fread(&c.name, 32, 1, f) == 1;
        ok = ok && fread(&c.type, 1, 1, f) == 1;
        ok = ok && fread(&c.flags, 1, 1, f) == 1;
        ok = ok && fread(&unused, 1, 1, f) == 1;
        ok = ok && fread(&c.sync_since, 4, 1, f) == 1;
        ok = ok && fread(&c.out_path_len, 1, 1, f) == 1;
        ok = ok && fread(&c.last_advert_timestamp, 4, 1, f) == 1;
        ok = ok && fread(c.out_path, 64, 1, f) == 1;
        ok = ok && fread(&c.lastmod, 4, 1, f) == 1;
        ok = ok && fread(&c.gps_lat, 4, 1, f) == 1;
        ok = ok && fread(&c.gps_lon, 4, 1, f) == 1;

        if (!ok) break;
        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
    }
    fclose(f);
}

void LinuxDataStore::saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c)) {
    char path[512]; fullPath(path, sizeof(path), "/contacts3");
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint32_t idx = 0;
    ContactInfo c;
    uint8_t unused = 0;

    while (host->getContactForSave(idx, c)) {
        if (filter && !filter(c)) { idx++; continue; }
        bool ok = fwrite(c.id.pub_key, 32, 1, f) == 1;
        ok = ok && fwrite(&c.name, 32, 1, f) == 1;
        ok = ok && fwrite(&c.type, 1, 1, f) == 1;
        ok = ok && fwrite(&c.flags, 1, 1, f) == 1;
        ok = ok && fwrite(&unused, 1, 1, f) == 1;
        ok = ok && fwrite(&c.sync_since, 4, 1, f) == 1;
        ok = ok && fwrite(&c.out_path_len, 1, 1, f) == 1;
        ok = ok && fwrite(&c.last_advert_timestamp, 4, 1, f) == 1;
        ok = ok && fwrite(c.out_path, 64, 1, f) == 1;
        ok = ok && fwrite(&c.lastmod, 4, 1, f) == 1;
        ok = ok && fwrite(&c.gps_lat, 4, 1, f) == 1;
        ok = ok && fwrite(&c.gps_lon, 4, 1, f) == 1;
        if (!ok) break;
        idx++;
    }
    fclose(f);
}

// ---- channels --------------------------------------------------------------

void LinuxDataStore::loadChannels(DataStoreHost* host) {
    char path[512]; fullPath(path, sizeof(path), "/channels2");
    FILE* f = fopen(path, "rb");
    if (!f) return;

    uint8_t channel_idx = 0;
    while (true) {
        ChannelDetails ch;
        uint8_t unused[4];
        bool ok = fread(unused, 4, 1, f) == 1;
        ok = ok && fread(&ch.name, 32, 1, f) == 1;
        ok = ok && fread(&ch.channel.secret, 32, 1, f) == 1;
        if (!ok) break;
        if (!host->onChannelLoaded(channel_idx, ch)) break;
        channel_idx++;
    }
    fclose(f);
}

void LinuxDataStore::saveChannels(DataStoreHost* host) {
    char path[512]; fullPath(path, sizeof(path), "/channels2");
    FILE* f = fopen(path, "wb");
    if (!f) return;

    uint8_t channel_idx = 0;
    uint8_t unused[4] = {};
    ChannelDetails ch;
    while (host->getChannelForSave(channel_idx, ch)) {
        bool ok = fwrite(unused, 4, 1, f) == 1;
        ok = ok && fwrite(&ch.name, 32, 1, f) == 1;
        ok = ok && fwrite(&ch.channel.secret, 32, 1, f) == 1;
        if (!ok) break;
        channel_idx++;
    }
    fclose(f);
}

// ---- blob store ------------------------------------------------------------

uint8_t LinuxDataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
    char path[512];
    blobPath(path, sizeof(path), _base, key, key_len);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    int len = (int)fread(dest_buf, 1, 255, f);
    fclose(f);
    return (uint8_t)(len > 0 ? len : 0);
}

bool LinuxDataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
    char path[512];
    blobPath(path, sizeof(path), _base, key, key_len);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(src_buf, 1, len, f) == len;
    fclose(f);
    if (!ok) remove(path);
    return ok;
}

bool LinuxDataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
    char path[512];
    blobPath(path, sizeof(path), _base, key, key_len);
    remove(path);
    return true;
}

// ---- storage stats ---------------------------------------------------------

uint32_t LinuxDataStore::getStorageUsedKb() const {
    // Walk the base dir and sum file sizes
    uint64_t total = 0;
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "du -sk %s 2>/dev/null | awk '{print $1}'", _base);
    FILE* p = popen(cmd, "r");
    if (p) { uint64_t kb = 0; if (fscanf(p, "%llu", &kb) == 1) total = kb; pclose(p); }
    return (uint32_t)total;
}

uint32_t LinuxDataStore::getStorageTotalKb() const {
    // Report available space on the filesystem containing _base
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "df -k %s 2>/dev/null | awk 'NR==2{print $4}'", _base);
    FILE* p = popen(cmd, "r");
    uint64_t kb = 0;
    if (p) { fscanf(p, "%llu", &kb); pclose(p); }
    return (uint32_t)(kb > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : kb);
}
