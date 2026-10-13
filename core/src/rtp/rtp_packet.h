#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace imago {

// View into one RTP packet (RFC 3550). Does not own the bytes.
struct RtpPacket {
    uint8_t payloadType = 0;
    bool marker = false;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
};

// Handles CSRC lists, header extensions and padding. Returns nullopt for malformed packets.
std::optional<RtpPacket> parseRtp(const uint8_t* data, size_t size);

} // namespace imago
