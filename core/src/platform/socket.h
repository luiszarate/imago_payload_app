#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace imago::net {

// A socket as the OS knows it: a file descriptor on POSIX, a SOCKET on Windows.
#if defined(_WIN32)
using SocketHandle = std::uintptr_t;
inline constexpr SocketHandle kInvalidSocket = ~SocketHandle{0}; // INVALID_SOCKET
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

// IPv4 address and port. The Orus L and every datalink we target are IPv4-only.
struct Endpoint {
    uint32_t address = 0; // host byte order
    uint16_t port = 0;

    static std::optional<Endpoint> parse(std::string_view ip, uint16_t port);
    // Resolves a hostname or dotted IPv4 address. Blocking; call only off the hot path.
    static std::optional<Endpoint> resolve(const std::string& host, uint16_t port);

    bool valid() const { return address != 0 && port != 0; }
    std::string addressString() const;
    std::string toString() const;
    bool operator==(const Endpoint&) const = default;
};

// Marks outgoing packets so Wi-Fi and IP networks can prioritise them (WMM / DSCP).
enum class TrafficClass {
    Control,    // gimbal and camera commands: lowest latency queue
    Video,      // RTSP control and RTCP
    BestEffort,
};

// Non-blocking UDP socket. send() may be called from any thread; receive from one thread only.
class UdpSocket {
  public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Binds to INADDR_ANY:localPort (0 = any free port).
    bool open(uint16_t localPort, int receiveBufferBytes, TrafficClass trafficClass,
              std::string* error);
    void close();
    bool isOpen() const { return fd_ != kInvalidSocket; }
    SocketHandle fd() const { return fd_; }
    uint16_t localPort() const;

    // Returns bytes sent, or -1 on error.
    long send(const Endpoint& to, const void* data, size_t size) const;
    // Returns bytes received, 0 if nothing is pending, -1 on error.
    long receive(void* buffer, size_t capacity, Endpoint* from) const;

  private:
    SocketHandle fd_ = kInvalidSocket;
};

// Non-blocking TCP socket.
class TcpSocket {
  public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    bool connect(const Endpoint& to, int timeoutMs, TrafficClass trafficClass, std::string* error);
    void close();
    bool isOpen() const { return fd_ != kInvalidSocket; }
    SocketHandle fd() const { return fd_; }

    // Sends everything or fails. Waits at most timeoutMs for buffer space.
    bool sendAll(const void* data, size_t size, int timeoutMs) const;
    // Returns bytes received, 0 if nothing is pending, -1 on error or orderly close.
    long receive(void* buffer, size_t capacity) const;

  private:
    friend class TcpListener;
    SocketHandle fd_ = kInvalidSocket;
};

// Listening TCP socket on INADDR_ANY (the simulator's RTSP server).
class TcpListener {
  public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // port 0 = any free port.
    bool open(uint16_t port, std::string* error);
    void close();
    bool isOpen() const { return fd_ != kInvalidSocket; }
    SocketHandle fd() const { return fd_; }
    uint16_t localPort() const;

    // Accepts a pending connection as a non-blocking socket with TCP_NODELAY. false if none.
    bool accept(TcpSocket* client, Endpoint* peer) const;

  private:
    SocketHandle fd_ = kInvalidSocket;
};

// Waits until one of the sockets is readable or the timeout expires.
// readable[i] is set for each ready socket. Returns the number of ready sockets,
// 0 on timeout, -1 on error. kInvalidSocket entries are ignored.
int waitReadable(std::span<const SocketHandle> fds, int timeoutMs, std::span<bool> readable);

} // namespace imago::net
