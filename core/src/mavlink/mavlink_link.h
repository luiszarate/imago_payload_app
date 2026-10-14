#pragma once

#include "mavlink/mavlink.h"
#include "mavlink/tlog_writer.h"
#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace imago {

// MAVLink v2 over one UDP socket.
//
// Sending is thread-safe and immediate: each send() encodes under a short lock and calls
// sendto() on the caller's thread. There is no send queue, so a command leaves the device in
// microseconds instead of waiting for a writer thread.
//
// Receiving (poll) must be called from a single thread.
class MavlinkLink {
  public:
    using MessageHandler = std::function<void(const mavlink_message_t&)>;

    // remote invalid (address 0) = listen mode: the first sender of valid MAVLink becomes the
    // peer, and later senders replace it, so a restarted ground unit is picked up again.
    bool open(const net::Endpoint& remote, uint16_t localPort, uint8_t systemId,
              uint8_t componentId, std::string* error);
    void close();
    bool isOpen() const { return socket_.isOpen(); }

    uint8_t systemId() const { return systemId_; }
    uint8_t componentId() const { return componentId_; }
    // Current MAVLink peer; invalid until known in listen mode.
    net::Endpoint peer() const;

    bool send(const mavlink_heartbeat_t& m);
    bool send(const mavlink_command_long_t& m);
    bool send(const mavlink_param_ext_set_t& m);
    bool send(const mavlink_param_ext_request_list_t& m);
    bool send(const mavlink_param_ext_request_read_t& m);
    bool send(const mavlink_gimbal_device_set_attitude_t& m);
    bool send(const mavlink_gimbal_manager_set_attitude_t& m);

    // Waits up to timeoutMs for datagrams, then parses everything pending and calls handler
    // once per complete message. Returns the number of messages, or -1 on socket error.
    int poll(int timeoutMs, const MessageHandler& handler);

    // Every packet sent and received is also handed to this recorder; nullptr stops it.
    void setRecorder(std::shared_ptr<TlogWriter> recorder);

    uint32_t messagesReceived() const { return rxMessages_.load(std::memory_order_relaxed); }
    uint32_t messagesSent() const { return txMessages_.load(std::memory_order_relaxed); }
    uint32_t messagesDropped() const { return rxDropped_.load(std::memory_order_relaxed); }

  private:
    template <typename Encode> bool encodeAndSend(Encode encode);
    void record(const uint8_t* packet, size_t length);

    net::UdpSocket socket_;
    bool listenMode_ = false;
    net::Endpoint remote_; // guarded by txMutex_
    uint8_t systemId_ = 1;
    uint8_t componentId_ = MAV_COMP_ID_ONBOARD_COMPUTER3;

    mutable std::mutex txMutex_;
    mavlink_status_t txStatus_{};

    mavlink_status_t rxStatus_{};
    mavlink_message_t rxBuffer_{};
    // Last sequence number per (system, component) and per system. Senders differ: some keep
    // one counter per component, others share one across all their components. The smaller of
    // the two gaps is counted, so the metric never reports loss that did not happen.
    uint8_t lastSeq_[256][256] = {};
    bool seqKnown_[256][256] = {};
    uint8_t lastSystemSeq_[256] = {};
    bool systemSeqKnown_[256] = {};

    std::mutex recorderMutex_;
    std::shared_ptr<TlogWriter> recorder_; // guarded by recorderMutex_
    std::atomic<bool> recording_{false};    // skips all of it when nobody records

    std::atomic<uint32_t> rxMessages_{0};
    std::atomic<uint32_t> txMessages_{0};
    std::atomic<uint32_t> rxDropped_{0};
};

} // namespace imago
