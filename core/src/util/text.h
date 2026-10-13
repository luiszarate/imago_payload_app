#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imago::text {

std::string_view trim(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool istartsWith(std::string_view s, std::string_view prefix);
std::vector<std::string_view> split(std::string_view s, char separator);
std::optional<long long> parseInt(std::string_view s);

// Copies src into a fixed char array, always NUL-terminated.
template <size_t N> void copyTo(char (&dst)[N], std::string_view src) {
    const size_t n = src.size() < N - 1 ? src.size() : N - 1;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
    dst[n] = '\0';
}

} // namespace imago::text
