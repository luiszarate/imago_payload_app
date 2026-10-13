#include "util/base64.h"

#include <array>

namespace imago {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::array<int8_t, 256> makeDecodeTable() {
    std::array<int8_t, 256> table{};
    for (auto& v : table) {
        v = -1;
    }
    for (int i = 0; i < 64; ++i) {
        table[static_cast<uint8_t>(kAlphabet[i])] = static_cast<int8_t>(i);
    }
    return table;
}

constexpr auto kDecode = makeDecodeTable();

} // namespace

std::optional<std::vector<uint8_t>> base64Decode(std::string_view text) {
    while (!text.empty() && text.back() == '=') {
        text.remove_suffix(1);
    }
    if (text.size() % 4 == 1) {
        return std::nullopt;
    }
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int8_t value = kDecode[static_cast<uint8_t>(c)];
        if (value < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string base64Encode(std::string_view data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(data[i]) << 16) |
                           (static_cast<uint8_t>(data[i + 1]) << 8) | static_cast<uint8_t>(data[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    const size_t rest = data.size() - i;
    if (rest == 1) {
        const uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (rest == 2) {
        const uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

} // namespace imago
