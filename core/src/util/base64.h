#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imago {

// Standard alphabet, padding optional. Whitespace is rejected.
std::optional<std::vector<uint8_t>> base64Decode(std::string_view text);
std::string base64Encode(std::string_view data);

} // namespace imago
