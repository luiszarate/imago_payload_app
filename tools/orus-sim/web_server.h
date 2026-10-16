#pragma once

// The Orus L's web settings page as far as the core uses it: GET /setup-ir-camera with the
// current isotherm values in the form, POST of the "Isotherms Regions" section, and
// POST /setup-systems for the restarts. Our own minimal page with the field names of
// firmware 3.0.5 (docs/protocol/orus-l.md, "Web settings"); one connection at a time, like
// the payload's Werkzeug server.

#include "platform/socket.h"

#include <atomic>
#include <cstdint>
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

  private:
    void run();
    void serve(const net::TcpSocket& socket);
    std::string page() const;

    net::TcpListener listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    Isotherm isotherm_; // guarded by mutex_
    std::atomic<int> restarts_{0};
    std::atomic<int> reboots_{0};
    std::atomic<int> posts_{0};
};

} // namespace imago::sim
