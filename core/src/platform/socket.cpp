#include "platform/socket.h"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mutex>
#if !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace imago::net {

namespace {

// ----------------------------------------------------------------------------------------------
// What differs between POSIX sockets and Winsock

#if defined(_WIN32)

using SockLen = int;

constexpr int kSendFlags = 0;

// Winsock must be started once per process before any other call. Never cleaned up: sockets
// may still close during static destruction.
void startWinsock() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

SOCKET native(SocketHandle s) { return static_cast<SOCKET>(s); }

int lastError() { return WSAGetLastError(); }

bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
}

bool connectInProgress() { return WSAGetLastError() == WSAEWOULDBLOCK; }

std::string errorText(int code) {
    char text[256] = {};
    const DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                   static_cast<DWORD>(code), 0, text, sizeof(text), nullptr);
    std::string result(text, n);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == '.')) {
        result.pop_back();
    }
    return result.empty() ? "error " + std::to_string(code) : result;
}

void closeSocket(SocketHandle s) { ::closesocket(native(s)); }

bool setNonBlocking(SocketHandle s) {
    u_long on = 1;
    return ::ioctlsocket(native(s), FIONBIO, &on) == 0;
}

// Windows ignores IP_TOS without an administrator policy, and its qWAVE API needs a connected
// flow per destination. Packets leave unmarked; latency on a local link does not depend on it.
void applyTrafficClass(SocketHandle, TrafficClass) {}

void disableSigpipe(SocketHandle) {}

// An ICMP "port unreachable" (the payload not listening yet) would otherwise fail the next
// recvfrom() with WSAECONNRESET, as if the socket itself were broken.
void ignoreConnectionReset(SocketHandle s) {
    BOOL off = FALSE;
    DWORD bytes = 0;
    WSAIoctl(native(s), SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
}

// UDP sockets need no reuse flag: UDP has no TIME_WAIT, and SO_REUSEADDR on Windows would let
// a second app bind the same port and silently take some of the datagrams.
void allowQuickRebind(SocketHandle, bool udp) { (void)udp; }

// The readiness wait for a non-blocking connect(). select() rather than WSAPoll(): WSAPoll did
// not report failed connections before Windows 10 2004.
int waitWritable(SocketHandle s, int timeoutMs) {
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(native(s), &writable);
    FD_SET(native(s), &failed);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return ::select(0, nullptr, &writable, &failed, &tv);
}

#else

using SockLen = socklen_t;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void startWinsock() {}

int native(SocketHandle s) { return s; }

int lastError() { return errno; }

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }

bool connectInProgress() { return errno == EINPROGRESS; }

std::string errorText(int code) { return std::strerror(code); }

void closeSocket(SocketHandle s) { ::close(s); }

bool setNonBlocking(SocketHandle s) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}

void applyTrafficClass(SocketHandle s, TrafficClass trafficClass) {
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
    setsockopt(s, SOL_SOCKET, SO_NET_SERVICE_TYPE, &service, sizeof(service));
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
        setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    }
#endif
}

void disableSigpipe(SocketHandle s) {
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)s;
#endif
}

void ignoreConnectionReset(SocketHandle) {}

void allowQuickRebind(SocketHandle s, bool) {
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
}

int waitWritable(SocketHandle s, int timeoutMs) {
    pollfd pfd{s, POLLOUT, 0};
    return ::poll(&pfd, 1, timeoutMs);
}

#endif

// ----------------------------------------------------------------------------------------------

template <typename T>
void setOption(SocketHandle s, int level, int name, T value) {
    setsockopt(native(s), level, name, reinterpret_cast<const char*>(&value), sizeof(value));
}

sockaddr_in toSockaddr(const Endpoint& e) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(e.address);
    addr.sin_port = htons(e.port);
    return addr;
}

Endpoint fromSockaddr(const sockaddr_in& addr) {
    return Endpoint{ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)};
}

SocketHandle openSocket(int type, int protocol) {
    startWinsock();
    const auto s = ::socket(AF_INET, type, protocol);
#if defined(_WIN32)
    return s == INVALID_SOCKET ? kInvalidSocket : static_cast<SocketHandle>(s);
#else
    return s < 0 ? kInvalidSocket : s;
#endif
}

void setError(std::string* error, const char* what, int code) {
    if (error != nullptr) {
        *error = std::string(what) + ": " + errorText(code);
    }
}

uint16_t boundPort(SocketHandle s) {
    sockaddr_in addr{};
    SockLen len = sizeof(addr);
    if (s == kInvalidSocket || getsockname(native(s), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

void closeHandle(SocketHandle& s) {
    if (s != kInvalidSocket) {
        closeSocket(s);
        s = kInvalidSocket;
    }
}

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
    startWinsock();
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

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalidSocket; }

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = kInvalidSocket;
    }
    return *this;
}

bool UdpSocket::open(uint16_t localPort, int receiveBufferBytes, TrafficClass trafficClass,
                     std::string* error) {
    close();
    fd_ = openSocket(SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ == kInvalidSocket) {
        setError(error, "socket", lastError());
        return false;
    }
    allowQuickRebind(fd_, true);
    if (receiveBufferBytes > 0) {
        // Large buffer absorbs the burst of a keyframe; the OS may clamp the value.
        setOption(fd_, SOL_SOCKET, SO_RCVBUF, receiveBufferBytes);
    }
    applyTrafficClass(fd_, trafficClass);
    ignoreConnectionReset(fd_);
    if (!setNonBlocking(fd_)) {
        setError(error, "non-blocking", lastError());
        close();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(localPort);
    if (::bind(native(fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        setError(error, "bind", lastError());
        close();
        return false;
    }
    return true;
}

void UdpSocket::close() { closeHandle(fd_); }

uint16_t UdpSocket::localPort() const { return boundPort(fd_); }

long UdpSocket::send(const Endpoint& to, const void* data, size_t size) const {
    if (fd_ == kInvalidSocket) {
        return -1;
    }
    const sockaddr_in addr = toSockaddr(to);
    const auto sent = ::sendto(native(fd_), static_cast<const char*>(data), static_cast<int>(size),
                               kSendFlags, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return static_cast<long>(sent);
}

long UdpSocket::receive(void* buffer, size_t capacity, Endpoint* from) const {
    if (fd_ == kInvalidSocket) {
        return -1;
    }
    sockaddr_in addr{};
    SockLen len = sizeof(addr);
    const auto received = ::recvfrom(native(fd_), static_cast<char*>(buffer), static_cast<int>(capacity),
                                     0, reinterpret_cast<sockaddr*>(&addr), &len);
    if (received < 0) {
        return wouldBlock() ? 0 : -1;
    }
    if (from != nullptr) {
        *from = fromSockaddr(addr);
    }
    return static_cast<long>(received);
}

// ----------------------------------------------------------------------------------------------
// TcpSocket

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalidSocket; }

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = kInvalidSocket;
    }
    return *this;
}

bool TcpSocket::connect(const Endpoint& to, int timeoutMs, TrafficClass trafficClass,
                        std::string* error) {
    close();
    fd_ = openSocket(SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == kInvalidSocket) {
        setError(error, "socket", lastError());
        return false;
    }
    // RTSP requests and interleaved RTP are small and latency-sensitive.
    setOption(fd_, IPPROTO_TCP, TCP_NODELAY, 1);
    setOption(fd_, SOL_SOCKET, SO_RCVBUF, 4 * 1024 * 1024);
    disableSigpipe(fd_);
    applyTrafficClass(fd_, trafficClass);
    if (!setNonBlocking(fd_)) {
        setError(error, "non-blocking", lastError());
        close();
        return false;
    }
    const sockaddr_in addr = toSockaddr(to);
    if (::connect(native(fd_), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (!connectInProgress()) {
            setError(error, "connect", lastError());
            close();
            return false;
        }
        const int ready = waitWritable(fd_, timeoutMs);
        if (ready <= 0) {
            if (error != nullptr) {
                *error = "connect: timed out";
            }
            close();
            return false;
        }
        int soError = 0;
        SockLen len = sizeof(soError);
        getsockopt(native(fd_), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
        if (soError != 0) {
            setError(error, "connect", soError);
            close();
            return false;
        }
    }
    return true;
}

void TcpSocket::close() { closeHandle(fd_); }

bool TcpSocket::sendAll(const void* data, size_t size, int timeoutMs) const {
    if (fd_ == kInvalidSocket) {
        return false;
    }
    const auto* bytes = static_cast<const char*>(data);
    size_t offset = 0;
    while (offset < size) {
        const auto sent = ::send(native(fd_), bytes + offset, static_cast<int>(size - offset), kSendFlags);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && !wouldBlock()) {
            return false;
        }
        if (waitWritable(fd_, timeoutMs) <= 0) {
            return false;
        }
    }
    return true;
}

long TcpSocket::receive(void* buffer, size_t capacity) const {
    if (fd_ == kInvalidSocket) {
        return -1;
    }
    const auto received = ::recv(native(fd_), static_cast<char*>(buffer), static_cast<int>(capacity), 0);
    if (received == 0) {
        return -1; // peer closed
    }
    if (received < 0) {
        return wouldBlock() ? 0 : -1;
    }
    return static_cast<long>(received);
}

// ----------------------------------------------------------------------------------------------
// TcpListener

TcpListener::~TcpListener() { close(); }

bool TcpListener::open(uint16_t port, std::string* error) {
    close();
    fd_ = openSocket(SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == kInvalidSocket) {
        setError(error, "socket", lastError());
        return false;
    }
    allowQuickRebind(fd_, false);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(native(fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(native(fd_), 4) != 0) {
        setError(error, "listen", lastError());
        close();
        return false;
    }
    return true;
}

void TcpListener::close() { closeHandle(fd_); }

uint16_t TcpListener::localPort() const { return boundPort(fd_); }

bool TcpListener::accept(TcpSocket* client, Endpoint* peer) const {
    if (fd_ == kInvalidSocket) {
        return false;
    }
    sockaddr_in addr{};
    SockLen len = sizeof(addr);
    const auto accepted = ::accept(native(fd_), reinterpret_cast<sockaddr*>(&addr), &len);
#if defined(_WIN32)
    if (accepted == INVALID_SOCKET) {
        return false;
    }
#else
    if (accepted < 0) {
        return false;
    }
#endif
    TcpSocket socket;
    socket.fd_ = static_cast<SocketHandle>(accepted);
    setOption(socket.fd_, IPPROTO_TCP, TCP_NODELAY, 1);
    disableSigpipe(socket.fd_);
    // Non-blocking, so a peer that stops reading times out in sendAll() instead of blocking.
    setNonBlocking(socket.fd_);
    if (peer != nullptr) {
        *peer = fromSockaddr(addr);
    }
    *client = std::move(socket);
    return true;
}

// ----------------------------------------------------------------------------------------------

int waitReadable(std::span<const SocketHandle> fds, int timeoutMs, std::span<bool> readable) {
    constexpr size_t kMax = 8;
    size_t map[kMax];
    size_t count = 0;
    timeoutMs = timeoutMs < 0 ? 0 : timeoutMs;
#if defined(_WIN32)
    fd_set readSet;
    fd_set errorSet;
    FD_ZERO(&readSet);
    FD_ZERO(&errorSet);
    for (size_t i = 0; i < fds.size() && count < kMax; ++i) {
        if (i < readable.size()) {
            readable[i] = false;
        }
        if (fds[i] != kInvalidSocket) {
            FD_SET(native(fds[i]), &readSet);
            FD_SET(native(fds[i]), &errorSet);
            map[count++] = i;
        }
    }
    if (count == 0) {
        return 0;
    }
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int ready = ::select(0, &readSet, nullptr, &errorSet, &tv);
    if (ready <= 0) {
        return ready < 0 && WSAGetLastError() == WSAEINTR ? 0 : (ready < 0 ? -1 : 0);
    }
    int result = 0;
    for (size_t i = 0; i < count; ++i) {
        const SOCKET s = native(fds[map[i]]);
        if (FD_ISSET(s, &readSet) || FD_ISSET(s, &errorSet)) {
            if (map[i] < readable.size()) {
                readable[map[i]] = true;
            }
            ++result;
        }
    }
    return result;
#else
    pollfd pfds[kMax];
    for (size_t i = 0; i < fds.size() && count < kMax; ++i) {
        if (i < readable.size()) {
            readable[i] = false;
        }
        if (fds[i] != kInvalidSocket) {
            pfds[count] = pollfd{fds[i], POLLIN, 0};
            map[count] = i;
            ++count;
        }
    }
    if (count == 0) {
        return 0;
    }
    const int ready = ::poll(pfds, static_cast<nfds_t>(count), timeoutMs);
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
#endif
}

} // namespace imago::net
