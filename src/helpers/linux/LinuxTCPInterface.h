#pragma once

#include <helpers/BaseSerialInterface.h>

// TCP-socket implementation of BaseSerialInterface for the Linux virtual
// companion.  Listens on a configurable port and accepts a single companion
// connection at a time.  Uses the same length-prefixed framing as the
// Arduino companion_radio WiFi transport:
//   inbound  (app → node):  '<' + 2-byte LE length + payload
//   outbound (node → app):  '>' + 2-byte LE length + payload
class LinuxTCPInterface : public BaseSerialInterface {
public:
    explicit LinuxTCPInterface(int port = 5000);
    ~LinuxTCPInterface();

    void   setPort(int port) { _port = port; }

    void   begin();
    void   enable()  override;
    void   disable() override;
    bool   isEnabled()    const override { return _enabled; }
    bool   isConnected()  const override { return _client_fd >= 0; }
    bool   isWriteBusy()  const override { return false; }

    size_t writeFrame(const uint8_t src[], size_t len) override;
    size_t checkRecvFrame(uint8_t dest[]) override;

private:
    enum RecvState { IDLE, HDR_FOUND, LEN1_FOUND, ACCUMULATING };

    int        _port;
    int        _server_fd;
    int        _client_fd;
    bool       _enabled;

    RecvState  _state;
    uint16_t   _expected_len;
    uint16_t   _recv_len;
    uint8_t    _recv_buf[MAX_FRAME_SIZE + 4];

    void acceptClient();
    void closeClient();
    bool tryRead(uint8_t& out);
};
