#include "mavlink/mavlink_link.h"

#include <algorithm>
#include <cstring>

namespace imago {

bool MavlinkLink::open(const net::Endpoint& remote, uint16_t localPort, uint8_t systemId,
                       uint8_t componentId, std::string* error) {
    close();
    remote_ = remote;
    listenMode_ = remote.address == 0;
    systemId_ = systemId;
    componentId_ = componentId;
    std::memset(&txStatus_, 0, sizeof(txStatus_));
    std::memset(&rxStatus_, 0, sizeof(rxStatus_));
    std::memset(seqKnown_, 0, sizeof(seqKnown_));
    std::memset(systemSeqKnown_, 0, sizeof(systemSeqKnown_));
    // MAVLink traffic is light; 256 KiB covers bursts of parameter values.
    return socket_.open(localPort, 256 * 1024, net::TrafficClass::Control, error);
}

void MavlinkLink::close() { socket_.close(); }

net::Endpoint MavlinkLink::peer() const {
    std::lock_guard lock(txMutex_);
    return remote_;
}

bool MavlinkLink::sendKeepAwake(uint16_t port) {
    net::Endpoint to = peer();
    if (!to.valid()) {
        return false;
    }
    to.port = port != 0 ? port : 9;
    const uint8_t byte = 0;
    return socket_.send(to, &byte, 1) == 1;
}

template <typename Encode> bool MavlinkLink::encodeAndSend(Encode encode) {
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    uint16_t length = 0;
    net::Endpoint to;
    {
        std::lock_guard lock(txMutex_);
        if (!remote_.valid()) {
            return false; // listen mode, peer not known yet
        }
        mavlink_message_t message;
        encode(message);
        length = mavlink_msg_to_send_buffer(buffer, &message);
        to = remote_;
    }
    const bool ok = socket_.send(to, buffer, length) == static_cast<long>(length);
    if (ok) {
        txMessages_.fetch_add(1, std::memory_order_relaxed);
        record(buffer, length);
    }
    return ok;
}

void MavlinkLink::setRecorder(std::shared_ptr<TlogWriter> recorder) {
    std::lock_guard lock(recorderMutex_);
    recording_.store(recorder != nullptr, std::memory_order_relaxed);
    recorder_ = std::move(recorder);
}

void MavlinkLink::record(const uint8_t* packet, size_t length) {
    if (!recording_.load(std::memory_order_relaxed)) {
        return;
    }
    std::shared_ptr<TlogWriter> recorder;
    {
        std::lock_guard lock(recorderMutex_);
        recorder = recorder_;
    }
    if (recorder) {
        recorder->record(packet, length);
    }
}

bool MavlinkLink::send(const mavlink_heartbeat_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_heartbeat_encode_status(systemId_, componentId_, &txStatus_, &msg, &m);
    });
}

bool MavlinkLink::send(const mavlink_command_long_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_command_long_encode_status(systemId_, componentId_, &txStatus_, &msg, &m);
    });
}

bool MavlinkLink::send(const mavlink_param_ext_set_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_param_ext_set_encode_status(systemId_, componentId_, &txStatus_, &msg, &m);
    });
}

bool MavlinkLink::send(const mavlink_param_ext_request_list_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_param_ext_request_list_encode_status(systemId_, componentId_, &txStatus_, &msg,
                                                         &m);
    });
}

bool MavlinkLink::send(const mavlink_param_ext_request_read_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_param_ext_request_read_encode_status(systemId_, componentId_, &txStatus_, &msg,
                                                         &m);
    });
}

bool MavlinkLink::send(const mavlink_gimbal_device_set_attitude_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_gimbal_device_set_attitude_encode_status(systemId_, componentId_, &txStatus_,
                                                             &msg, &m);
    });
}

bool MavlinkLink::send(const mavlink_gimbal_manager_set_attitude_t& m) {
    return encodeAndSend([&](mavlink_message_t& msg) {
        mavlink_msg_gimbal_manager_set_attitude_encode_status(systemId_, componentId_, &txStatus_,
                                                             &msg, &m);
    });
}

int MavlinkLink::poll(int timeoutMs, const MessageHandler& handler) {
    const net::SocketHandle fd = socket_.fd();
    bool readable = false;
    if (net::waitReadable({&fd, 1}, timeoutMs, {&readable, 1}) < 0) {
        return -1;
    }
    if (!readable) {
        return 0;
    }
    int handled = 0;
    uint8_t datagram[2048];
    while (true) {
        net::Endpoint from;
        const long size = socket_.receive(datagram, sizeof(datagram), &from);
        if (size < 0) {
            return -1;
        }
        if (size == 0) {
            break;
        }
        for (long i = 0; i < size; ++i) {
            mavlink_message_t message;
            mavlink_status_t status;
            const uint8_t result =
                mavlink_frame_char_buffer(&rxBuffer_, &rxStatus_, datagram[i], &message, &status);
            if (result == MAVLINK_FRAMING_BAD_CRC || result == MAVLINK_FRAMING_BAD_SIGNATURE) {
                rxDropped_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (result != MAVLINK_FRAMING_OK) {
                continue;
            }
            // Extended 32-bit system IDs are never used by Gremsy payloads or Herelink.
            if (message.sysid > 255) {
                continue;
            }
            if (recording_.load(std::memory_order_relaxed)) {
                uint8_t packet[MAVLINK_MAX_PACKET_LEN];
                record(packet, mavlink_msg_to_send_buffer(packet, &message));
            }
            if (listenMode_ && (message.sysid != systemId_ || message.compid != componentId_)) {
                std::lock_guard lock(txMutex_);
                remote_ = from;
            }
            // Sequence gaps reveal packet loss on the datalink.
            const auto sys = static_cast<uint8_t>(message.sysid);
            const uint8_t comp = message.compid;
            if (seqKnown_[sys][comp] && systemSeqKnown_[sys]) {
                const auto componentGap = static_cast<uint8_t>(message.seq - lastSeq_[sys][comp] - 1);
                const auto systemGap = static_cast<uint8_t>(message.seq - lastSystemSeq_[sys] - 1);
                const uint8_t gap = std::min(componentGap, systemGap);
                if (gap != 0 && gap < 128) {
                    rxDropped_.fetch_add(gap, std::memory_order_relaxed);
                }
            }
            seqKnown_[sys][comp] = true;
            lastSeq_[sys][comp] = message.seq;
            systemSeqKnown_[sys] = true;
            lastSystemSeq_[sys] = message.seq;

            rxMessages_.fetch_add(1, std::memory_order_relaxed);
            ++handled;
            handler(message);
        }
    }
    return handled;
}

} // namespace imago
