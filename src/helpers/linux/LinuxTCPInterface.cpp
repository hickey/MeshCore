#include <helpers/linux/LinuxTCPInterface.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

LinuxTCPInterface::LinuxTCPInterface(int port)
    : _port(port), _server_fd(-1), _client_fd(-1), _enabled(false),
      _state(IDLE), _expected_len(0), _recv_len(0)
{}

LinuxTCPInterface::~LinuxTCPInterface() {
    closeClient();
    if (_server_fd >= 0) { ::close(_server_fd); _server_fd = -1; }
}

void LinuxTCPInterface::begin() {
    _server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_server_fd < 0) { perror("TCPInterface: socket"); return; }

    int opt = 1;
    setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)_port);

    if (::bind(_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("TCPInterface: bind"); ::close(_server_fd); _server_fd = -1; return;
    }
    if (::listen(_server_fd, 1) < 0) {
        perror("TCPInterface: listen"); ::close(_server_fd); _server_fd = -1; return;
    }

    // Non-blocking accept
    int flags = fcntl(_server_fd, F_GETFL, 0);
    fcntl(_server_fd, F_SETFL, flags | O_NONBLOCK);

    _enabled = true;
    printf("TCPInterface: listening on port %d\n", _port);
}

void LinuxTCPInterface::enable()  { _enabled = true; }
void LinuxTCPInterface::disable() { closeClient(); _enabled = false; }

void LinuxTCPInterface::acceptClient() {
    int fd = ::accept(_server_fd, nullptr, nullptr);
    if (fd < 0) return;
    closeClient();
    _client_fd = fd;
    // Set non-blocking
    int flags = fcntl(_client_fd, F_GETFL, 0);
    fcntl(_client_fd, F_SETFL, flags | O_NONBLOCK);
    _state       = IDLE;
    _recv_len    = 0;
    _expected_len = 0;
    printf("TCPInterface: client connected\n");
}

void LinuxTCPInterface::closeClient() {
    if (_client_fd >= 0) {
        ::close(_client_fd);
        _client_fd = -1;
        _state = IDLE;
        printf("TCPInterface: client disconnected\n");
    }
}

bool LinuxTCPInterface::tryRead(uint8_t& out) {
    if (_client_fd < 0) return false;
    ssize_t n = ::recv(_client_fd, &out, 1, 0);
    if (n == 1) return true;
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) closeClient();
    return false;
}

size_t LinuxTCPInterface::checkRecvFrame(uint8_t dest[]) {
    if (!_enabled) return 0;
    if (_server_fd >= 0 && _client_fd < 0) acceptClient();
    if (_client_fd < 0) return 0;

    uint8_t b;
    while (tryRead(b)) {
        switch (_state) {
        case IDLE:
            if (b == '<') _state = HDR_FOUND;
            break;
        case HDR_FOUND:
            _expected_len  = b;          // low byte
            _state = LEN1_FOUND;
            break;
        case LEN1_FOUND:
            _expected_len |= ((uint16_t)b << 8); // high byte
            _recv_len = 0;
            if (_expected_len == 0 || _expected_len > MAX_FRAME_SIZE) {
                _state = IDLE;           // bad length, resync
            } else {
                _state = ACCUMULATING;
            }
            break;
        case ACCUMULATING:
            if (_recv_len < sizeof(_recv_buf)) _recv_buf[_recv_len] = b;
            _recv_len++;
            if (_recv_len >= _expected_len) {
                size_t len = _recv_len <= MAX_FRAME_SIZE ? _recv_len : 0;
                if (len) memcpy(dest, _recv_buf, len);
                _state = IDLE;
                return len;
            }
            break;
        }
    }
    return 0;
}

size_t LinuxTCPInterface::writeFrame(const uint8_t src[], size_t len) {
    if (!_enabled || _client_fd < 0) return 0;

    uint8_t hdr[3];
    hdr[0] = '>';
    hdr[1] = (uint8_t)(len & 0xFF);
    hdr[2] = (uint8_t)((len >> 8) & 0xFF);

    if (::send(_client_fd, hdr, 3, MSG_NOSIGNAL) != 3) { closeClient(); return 0; }
    ssize_t sent = ::send(_client_fd, src, len, MSG_NOSIGNAL);
    if (sent < 0) { closeClient(); return 0; }
    return (size_t)sent;
}
