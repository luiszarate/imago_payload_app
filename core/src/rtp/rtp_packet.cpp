#include "rtp/rtp_packet.h"

namespace imago {

std::optional<RtpPacket> parseRtp(const uint8_t* data, size_t size) {
    if (size < 12) {
        return std::nullopt;
    }
    const uint8_t version = data[0] >> 6;
    if (version != 2) {
        return std::nullopt;
    }
    const bool padding = (data[0] & 0x20) != 0;
    const bool extension = (data[0] & 0x10) != 0;
    const size_t csrcCount = data[0] & 0x0F;

    RtpPacket p;
    p.marker = (data[1] & 0x80) != 0;
    p.payloadType = data[1] & 0x7F;
    p.sequence = static_cast<uint16_t>((data[2] << 8) | data[3]);
    p.timestamp = (static_cast<uint32_t>(data[4]) << 24) | (static_cast<uint32_t>(data[5]) << 16) |
                  (static_cast<uint32_t>(data[6]) << 8) | data[7];
    p.ssrc = (static_cast<uint32_t>(data[8]) << 24) | (static_cast<uint32_t>(data[9]) << 16) |
             (static_cast<uint32_t>(data[10]) << 8) | data[11];

    size_t offset = 12 + csrcCount * 4;
    if (offset > size) {
        return std::nullopt;
    }
    if (extension) {
        if (offset + 4 > size) {
            return std::nullopt;
        }
        const size_t words = static_cast<size_t>((data[offset + 2] << 8) | data[offset + 3]);
        offset += 4 + words * 4;
        if (offset > size) {
            return std::nullopt;
        }
    }
    size_t end = size;
    if (padding) {
        const size_t pad = data[size - 1];
        if (pad == 0 || offset + pad > size) {
            return std::nullopt;
        }
        end -= pad;
    }
    p.payload = data + offset;
    p.payloadSize = end - offset;
    return p;
}

} // namespace imago
