#pragma once
#include <Arduino.h>
#include <Mesh.h>

#define PERM_ACL_ROLE_MASK     3
#define PERM_ACL_GUEST         0
#define PERM_ACL_READ_ONLY     1
#define PERM_ACL_READ_WRITE    2
#define PERM_ACL_ADMIN         3
#define OUT_PATH_UNKNOWN       0xFF

struct ClientInfo {
    mesh::Identity id;
    uint8_t permissions;
    uint8_t out_path_len;
    uint8_t out_path[MAX_PATH_SIZE];
    uint8_t shared_secret[PUB_KEY_SIZE];
    uint32_t last_timestamp;
    uint32_t last_activity;
    union {
        struct {
            uint32_t sync_since;
            uint32_t pending_ack;
            uint32_t push_post_timestamp;
            unsigned long ack_timeout;
            uint8_t push_failures;
        } room;
    } extra;

    bool isAdmin() const { return (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN; }
};

#ifndef MAX_CLIENTS
#define MAX_CLIENTS 20
#endif

// Stub — ACL persistence is filesystem-backed on Arduino but unused on Linux.
class ClientACL {
    ClientInfo _clients[MAX_CLIENTS];
    int _num;
public:
    ClientACL() : _num(0) { memset(_clients, 0, sizeof(_clients)); }

    // No-ops: filesystem not available on Linux.
    void load(void*, const mesh::LocalIdentity&) {}
    void save(void*, bool (*)(ClientInfo*) = nullptr) {}
    bool clear() { _num = 0; return true; }

    int         getNumClients()  const { return _num; }
    ClientInfo* getClientByIdx(int i)  { return &_clients[i]; }

    ClientInfo* getClient(const uint8_t* pubkey, int key_len) {
        for (int i = 0; i < _num; i++)
            if (memcmp(_clients[i].id.pub_key, pubkey, key_len) == 0) return &_clients[i];
        return nullptr;
    }

    ClientInfo* putClient(const mesh::Identity& id, uint8_t init_perms) {
        ClientInfo* existing = getClient(id.pub_key, sizeof(id.pub_key));
        if (existing) return existing;
        if (_num >= MAX_CLIENTS) return nullptr;
        ClientInfo& c = _clients[_num++];
        memset(&c, 0, sizeof(c));
        c.id = id;
        c.permissions = init_perms;
        return &c;
    }

    bool applyPermissions(const mesh::LocalIdentity&, const uint8_t* pubkey, int key_len, uint8_t perms) {
        ClientInfo* c = getClient(pubkey, key_len);
        if (!c) return false;
        c->permissions = perms;
        return true;
    }
};
