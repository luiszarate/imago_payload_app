#pragma once

// The Orus L's web server as far as the core uses it: GET /setup-ir-camera with the current
// isotherm values in the form, POST of the "Isotherms Regions" section, POST /setup-systems
// for the restarts, and the media files (GET /list-file, GET or HEAD /download/<name> with
// Range, GET /delete/<name>, POST /delete-all-images and /delete-all-videos). Our own minimal
// pages with the field names of firmware 3.0.5 and the links Gremsy's SDK reads
// (docs/protocol/orus-l.md, "Web settings" and "Media files"). One thread per connection,
// like Werkzeug's threaded server, so a download does not hold up the list.

#include "media_library.h"
#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace imago::sim {

class WebServer {
  public:
    ~WebServer();
    bool start(uint16_t port, std::string* error); // 0 = any free port
    void stop();
    uint16_t port() const { return listener_.localPort(); }

    struct Isotherm {
        bool enabled = true;
        int thresholdC = 35;
        int measurementRange = 0;
    };
    Isotherm isotherm() const;
    int restarts() const { return restarts_.load(); }
    int reboots() const { return reboots_.load(); }
    int posts() const { return posts_.load(); }

    MediaLibrary& media() { return media_; }
    // Downloads at most this many kB/s (0 = as fast as the network goes), to watch the
    // progress the way a datalink shows it.
    void setMediaRateKBps(int kbps) { mediaRateKBps_.store(kbps); }
    // Bytes of media sent so far (thumbnail reads included), for tests.
    int64_t mediaBytesSent() const { return mediaBytes_.load(); }

  private:
    struct Connection {
        net::TcpSocket socket;
        std::thread thread;
        std::atomic<bool> done{false};
    };
    void run();
    void serve(const net::TcpSocket& socket);
    std::string page() const;
    std::string mediaPage() const;
    // Sends the file (or its Range) with the head; the body throttled by mediaRateKBps_.
    void sendMedia(const net::TcpSocket& socket, const MediaLibrary::Item& item, bool head,
                   const std::string& range);

    net::TcpListener listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::list<std::unique_ptr<Connection>> connections_; // run() thread only
    mutable std::mutex mutex_;
    Isotherm isotherm_; // guarded by mutex_
    std::atomic<int> restarts_{0};
    std::atomic<int> reboots_{0};
    std::atomic<int> posts_{0};
    MediaLibrary media_;
    std::atomic<int> mediaRateKBps_{0};
    std::atomic<int64_t> mediaBytes_{0};
};

} // namespace imago::sim
