// UTF-8 for the terminal: one column per character (the CLI prints no wide characters).

#pragma once

#include <cstddef>
#include <string>

inline bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Terminal columns taken by s[from, to).
inline size_t columns(const std::string& s, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to && i < s.size(); ++i) {
        n += isContinuation(s[i]) ? 0 : 1;
    }
    return n;
}

inline size_t columns(const std::string& s) {
    return columns(s, 0, s.size());
}

// Byte offset `count` characters after `from`.
inline size_t advance(const std::string& s, size_t from, size_t count) {
    size_t i = from;
    for (; i < s.size() && count > 0; --count) {
        ++i;
        while (i < s.size() && isContinuation(s[i])) {
            ++i;
        }
    }
    return i;
}

// Byte offset of the character before `pos`.
inline size_t previous(const std::string& s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && isContinuation(s[pos])) {
        --pos;
    }
    return pos;
}
