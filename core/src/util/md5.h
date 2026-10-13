#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace imago {

// MD5 (RFC 1321). Used only for RTSP Digest authentication, never for security decisions.
class Md5 {
  public:
    Md5();
    void update(const void* data, size_t size);
    void update(std::string_view text) { update(text.data(), text.size()); }
    std::array<uint8_t, 16> finish();

    // Lowercase hex digest of text.
    static std::string hex(std::string_view text);

  private:
    void transform(const uint8_t block[64]);

    uint32_t state_[4];
    uint64_t length_ = 0; // bytes
    uint8_t buffer_[64] = {};
    size_t buffered_ = 0;
};

} // namespace imago
