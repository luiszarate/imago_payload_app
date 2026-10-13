#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace imago::test {

// Builds an RTP packet (version 2, no CSRC) around a payload.
inline std::vector<uint8_t> rtp(uint16_t seq, uint32_t ts, bool marker,
                                const std::vector<uint8_t>& payload, uint8_t pt = 96) {
    std::vector<uint8_t> p = {
        0x80,
        static_cast<uint8_t>((marker ? 0x80 : 0) | pt),
        static_cast<uint8_t>(seq >> 8),
        static_cast<uint8_t>(seq),
        static_cast<uint8_t>(ts >> 24),
        static_cast<uint8_t>(ts >> 16),
        static_cast<uint8_t>(ts >> 8),
        static_cast<uint8_t>(ts),
        0x12, 0x34, 0x56, 0x78,
    };
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

inline std::vector<uint8_t> bytes(std::initializer_list<int> values) {
    std::vector<uint8_t> out;
    for (int v : values) {
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

inline std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// AVCC encoding of one NAL unit: 4-byte length + bytes.
inline std::vector<uint8_t> avcc(const std::vector<uint8_t>& nal) {
    const auto n = static_cast<uint32_t>(nal.size());
    std::vector<uint8_t> out = {static_cast<uint8_t>(n >> 24), static_cast<uint8_t>(n >> 16),
                                static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n)};
    out.insert(out.end(), nal.begin(), nal.end());
    return out;
}

} // namespace imago::test
