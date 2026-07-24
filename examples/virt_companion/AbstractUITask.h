#pragma once
// Minimal AbstractUITask stub for the virtual companion build.
// The companion has no display; MyMesh is constructed with ui=NULL so none
// of these methods are ever called.  The declaration is needed because
// MyMesh.h takes an AbstractUITask* parameter.

#include <helpers/BaseSerialInterface.h>
#include "NodePrefs.h"

enum class UIEventType { none, contactMessage, channelMessage, roomMessage, newContactMessage, ack };

class AbstractUITask {
public:
    virtual ~AbstractUITask() = default;
    virtual void msgRead(int /*msgcount*/) {}
    virtual void newMsg(uint8_t /*path_len*/, const char* /*from_name*/,
                        const char* /*text*/, int /*msgcount*/) {}
    virtual void notify(UIEventType /*t*/ = UIEventType::none) {}
    virtual void loop() {}
    void setHasConnection(bool) {}
    bool hasConnection() const { return false; }
    bool isSerialEnabled() const { return false; }
};
