#include "platform/socket.h"

#if defined(_WIN32)
#error "Windows sockets are not implemented yet (phase 4)."
#endif

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace imago::net {

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

sockaddr_in toSockaddr(const Endpoint& e) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(e.address);
    addr.sin_port = htons(e.port);
    return addr;
}

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void applyTrafficClass(int fd, TrafficClass trafficClass) {
#if defined(__APPLE__) && defined(SO_NET_SERVICE_TYPE)
    int service = NET_SERVICE_TYPE_BE;
    switch (trafficClass) {
    case TrafficClass::Control:
        service = NET_SERVICE_TYPE_VO;
        break;
    case TrafficClass::Video:
        service = NET_SERVICE_TYPE_VI;
        break;
    case TrafficClass::BestEffort:
        break;
    }
    setsockopt(fd, SOL_SOCKET, SO_NET_SERVICE_TYPE, &service, sizeof(service));
#else
    // DSCP EF (46) for control, AF41 (34) for video, shifted into the TOS byte.
    int tos = 0;
    switch (trafficClass) {
    case TrafficClass::Control:
        tos = 46 << 2;
        break;
    case TrafficClass::Video:
        tos = 34 << 2;
        break;
    case TrafficClass::BestEffort:
        break;
    }
    if (tos != 0) {
        setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    }
#endif
}

void disableSigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

void setError(std::string* error, const char* what) {
    if (error != nullptr) {
        *error = std::string(what) + ": " + std::strerror(errno);
    }
}

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }

} // namespace

// ----------------------------------------------------------------------------------------------
// Endpoint

std::optional<Endpoint> Endpoint::parse(std::string_view ip, uint16_t port) {
    const std::string text(ip);
    in_addr addr{};
    if (inet_pton(AF_INET, text.c_str(), &addr) != 1) {
        return std::nullopt;
    }
    return Endpoint{ntohl(addr.s_addr), port};
}

std::optional<Endpoint> Endpoint::resolve(const std::string& host, uint16_t port) {
    if (auto direct = parse(host, port)) {
        return direct;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return std::nullopt;
    }
    const auto* sin = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    Endpoint e{ntohl(sin->sin_addr.s_addr), port};
    freeaddrinfo(result);
    return e;
}

std::string Endpoint::addressString() const {
    in_addr addr{};
    addr.s_addr = htonl(address);
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr, text, sizeof(text));
    return text;
}

std::string Endpoint::toString() const { return addressString() + ":" + std::to_string(port); }

// ----------------------------------------------------------------------------------------------
// UdpSocket

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool UdpSocket::open(uint16_t localPort, int receiveBufferBytes, TrafficClass trafficClass,
                     std::string* error) {
    close();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) {
        setError(error, "socket");
        return false;
    }
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (receiveBufferBytes > 0) {
        // Large buffer absorbs the burst of a keyframe; the OS may clamp the value.
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &receiveBufferBytes, sizeof(receiveBufferBytes));
    }
    applyTrafficClass(fd_, trafficClass);
    if (!setNonBlocking(fd_)) {
        setError(error, "fcntl");
        close();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(localPort);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        setError(error, "bind");
        close();
        return false;
    }
    return true;
}

void UdpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint16_t UdpSocket::localPort() const {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (fd_ < 0 || getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

long UdpSocket::send(const Endpoint& to, const void* data, size_t size) const {
    if (fd_ < 0) {
        return -1;
    }
    const sockaddr_in addr = toSockaddr(to);
    const auto sent = ::sendto(fd_, data, size, kSendFlags, reinterpret_cast<const sockaddr*>(&addr),
                               sizeof(addr));
    return static_cast<long>(sent);
}

long UdpSocket::receive(void* buffer, size_t capacity, Endpoint* from) const {
    if (fd_ < 0) {
        return -1;
    }
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    const auto received =
        ::recvfrom(fd_, buffer, capacity, 0, reinterpret_cast<sockaddr*>(&addr), &len);
    if (received < 0) {
        return wouldBlock() ? 0 : -1;
    }
    if (from != nullptr) {
        from->address = ntohl(addr.sin_addr.s_addr);
        from->port = ntohs(addr.sin_port);
    }
    return static_cast<long>(received);
}

// ----------------------------------------------------------------------------------------------
// TcpSocket

TcpSocket::~TcpSocket() { close(); }

bool TcpSocket::connect(const Endpoint& to, int timeoutMs, TrafficClass trafficClass,
                        std::string* error) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
        setError(error, "socket");
        return false;
    }
    int one = 1;
    // RTSP requests and interleaved RTP are small and latency-sensitive.
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int receiveBuffer = 4 * 1024 * 1024;
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &receiveBuffer, sizeof(receiveBuffer));
    disableSigpipe(fd_);
    applyTrafficClass(fd_, trafficClass);
    if (!setNonBlocking(fd_)) {
        setError(error, "fcntl");
        close();
        return false;
    }
    const sockaddr_in addr = toSockaddr(to);
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            setError(error, "connect");
            close();
            return false;
        }
        pollfd pfd{fd_, POLLOUT, 0};
        const int ready = ::poll(&pfd, 1, timeoutMs);
        if (ready <= 0) {
            if (error != nullptr) {
                *error = "connect: timed out";
            }
            close();
            return false;
        }
        int soError = 0;
        socklen_t len = sizeof(soError);
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &len);
        if (soError != 0) {
            errno = soError;
            setError(error, "connect");
            close();
            return false;
        }
    }
    return true;
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool TcpSocket::sendAll(const void* data, size_t size, int timeoutMs) const {
    if (fd_ < 0) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        const auto sent = ::send(fd_, bytes + offset, size - offset, kSendFlags);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && !wouldBlock()) {
            return false;
        }
        pollfd pfd{fd_, POLLOUT, 0};
        if (::poll(&pfd, 1, timeoutMs) <= 0) {
            return false;
        }
    }
    return true;
}

long TcpSocket::receive(void* buffer, size_t capacity) const {
    if (fd_ < 0) {
        return -1;
    }
    const auto received = ::recv(fd_, buffer, capacity, 0);
    if (received == 0) {
        return -1; // peer closed
    }
    if (received < 0) {
        return wouldBlock() ? 0 : -1;
    }
    return static_cast<long>(received);
}

// ----------------------------------------------------------------------------------------------

int waitReadable(std::span<const int> fds, int timeoutMs, std::span<bool> readable) {
    pollfd pfds[8];
    size_t map[8];
    size_t count = 0;
    for (size_t i = 0; i < fds.size() && count < 8; ++i) {
        if (i < readable.size()) {
            readable[i] = false;
        }
        if (fds[i] >= 0) {
            pfds[count] = pollfd{fds[i], POLLIN, 0};
            map[count] = i;
            ++count;
        }
    }
    if (count == 0) {
        return 0;
    }
    const int ready = ::poll(pfds, static_cast<nfds_t>(count), timeoutMs < 0 ? 0 : timeoutMs);
    if (ready <= 0) {
        return ready < 0 && errno == EINTR ? 0 : ready;
    }
    int result = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            if (map[i] < readable.size()) {
                readable[map[i]] = true;
            }
            ++result;
        }
    }
    return result;
}

} // namespace imago::net
