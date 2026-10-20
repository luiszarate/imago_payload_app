#include "web/orus_media.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace imago::web {

namespace {

char lowerChar(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lowerChar);
    return out;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = lowerChar(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t utcSeconds(int year, int month, int day, int hour, int minute, int second) {
    if (year < 1970 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return -1;
    }
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600 + minute * 60 + second;
}

int number(std::string_view digits) {
    int value = 0;
    for (const char c : digits) {
        value = value * 10 + (c - '0');
    }
    return value;
}

// One HTML attribute value, double or single quoted, starting at the quote.
std::string quoted(const std::string& html, size_t quote) {
    if (quote >= html.size() || (html[quote] != '"' && html[quote] != '\'')) {
        return {};
    }
    const size_t end = html.find(html[quote], quote + 1);
    return end == std::string::npos ? std::string{} : html.substr(quote + 1, end - quote - 1);
}

void addLinkedNames(const std::string& html, const std::string& lowered, std::string_view prefix,
                    std::vector<std::string>& names) {
    const std::string needle = "href="; // then a quoted link starting with the prefix
    size_t at = 0;
    while ((at = lowered.find(needle, at)) != std::string::npos) {
        at += needle.size();
        const std::string link = quoted(html, at);
        if (link.size() <= prefix.size() || link.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        std::string name = percentDecode(std::string_view(link).substr(prefix.size()));
        if (const size_t query = name.find('?'); query != std::string::npos) {
            name.resize(query);
        }
        if (!name.empty() && name.find('/') == std::string::npos &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(std::move(name));
        }
    }
}

} // namespace

MediaListing parseMediaListing(const std::string& html) {
    MediaListing listing;
    const std::string lowered = lower(html);
    listing.loginRequired = lowered.find("/smb-login") != std::string::npos ||
                            lowered.find("smb-username") != std::string::npos;
    // Every file has a delete link (the SDK reads only those); a download link may name the
    // same file, or one without a delete link.
    addLinkedNames(html, lowered, kMediaDeletePrefix, listing.names);
    addLinkedNames(html, lowered, kMediaDownloadPrefix, listing.names);

    const size_t select = lowered.find("id=\"storage-select\"");
    if (select != std::string::npos) {
        const size_t end = lowered.find("</select>", select);
        size_t at = select;
        while (end != std::string::npos && (at = lowered.find("<option", at)) != std::string::npos &&
               at < end) {
            const size_t tagEnd = lowered.find('>', at);
            if (tagEnd == std::string::npos || tagEnd > end) {
                break;
            }
            const std::string tag = lowered.substr(at, tagEnd - at);
            MediaStorage storage;
            if (const size_t v = tag.find("value="); v != std::string::npos) {
                storage.value = quoted(html, at + v + 6);
            }
            const size_t labelEnd = lowered.find('<', tagEnd);
            storage.label = html.substr(tagEnd + 1, (labelEnd == std::string::npos ? end : labelEnd) -
                                                        tagEnd - 1);
            while (!storage.label.empty() && std::isspace(static_cast<unsigned char>(storage.label.back()))) {
                storage.label.pop_back();
            }
            while (!storage.label.empty() && std::isspace(static_cast<unsigned char>(storage.label.front()))) {
                storage.label.erase(0, 1);
            }
            if (tag.find(" selected") != std::string::npos) {
                listing.currentStorage = static_cast<int>(listing.storages.size());
            }
            listing.storages.push_back(std::move(storage));
            at = tagEnd;
        }
        if (listing.storages.size() < 2) {
            listing.storages.clear(); // nothing to choose from
            listing.currentStorage = -1;
        }
    }
    return listing;
}

MediaKind mediaKindOf(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return MediaKind::Other;
    }
    const std::string ext = lower(name.substr(dot + 1));
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp") {
        return MediaKind::Photo;
    }
    if (ext == "mp4" || ext == "mov" || ext == "mkv" || ext == "avi" || ext == "ts") {
        return MediaKind::Video;
    }
    if (ext == "tif" || ext == "tiff") {
        return MediaKind::Radiometric;
    }
    return MediaKind::Other;
}

bool isThermalName(std::string_view name) {
    const std::string n = lower(name);
    for (size_t at = n.find("ir"); at != std::string::npos; at = n.find("ir", at + 1)) {
        const bool before = at == 0 || !std::isalpha(static_cast<unsigned char>(n[at - 1]));
        const bool after = at + 2 >= n.size() || !std::isalpha(static_cast<unsigned char>(n[at + 2]));
        if (before && after) {
            return true;
        }
    }
    return false;
}

MediaShot shotOf(std::string_view name) {
    // <prefix>_<EO|IR>_<digits>.<ext>
    MediaShot shot;
    const size_t dot = name.find_last_of('.');
    const std::string_view stem = name.substr(0, dot);
    const size_t last = stem.find_last_of('_');
    if (last == std::string_view::npos || last < 3 || last + 1 >= stem.size()) {
        return shot;
    }
    const std::string_view number = stem.substr(last + 1);
    if (!std::all_of(number.begin(), number.end(), isDigit)) {
        return shot;
    }
    const std::string camera = lower(stem.substr(last - 2, 2));
    if (stem[last - 3] != '_' || (camera != "eo" && camera != "ir")) {
        return shot;
    }
    shot.camera = camera == "eo" ? MediaCamera::Eo : MediaCamera::Ir;
    shot.key = std::string(stem.substr(0, last - 3)) + "_" + std::string(number) +
               (dot == std::string_view::npos ? std::string{} : lower(name.substr(dot)));
    return shot;
}

int64_t timeFromName(std::string_view name) {
    // The runs of digits in the name, e.g. "EO_20261019_110233.jpg" -> 20261019, 110233.
    std::vector<std::string_view> runs;
    for (size_t i = 0; i < name.size();) {
        if (!isDigit(name[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < name.size() && isDigit(name[j])) {
            ++j;
        }
        runs.push_back(name.substr(i, j - i));
        i = j;
    }
    for (size_t r = 0; r < runs.size(); ++r) {
        const std::string_view a = runs[r];
        if (a.size() == 14) {
            const int64_t t = utcSeconds(number(a.substr(0, 4)), number(a.substr(4, 2)),
                                         number(a.substr(6, 2)), number(a.substr(8, 2)),
                                         number(a.substr(10, 2)), number(a.substr(12, 2)));
            if (t >= 0) {
                return t;
            }
        }
        if (a.size() == 8 && r + 1 < runs.size() && runs[r + 1].size() == 6) {
            const std::string_view b = runs[r + 1];
            const int64_t t = utcSeconds(number(a.substr(0, 4)), number(a.substr(4, 2)),
                                         number(a.substr(6, 2)), number(b.substr(0, 2)),
                                         number(b.substr(2, 2)), number(b.substr(4, 2)));
            if (t >= 0) {
                return t;
            }
        }
        if (a.size() == 4 && r + 5 < runs.size() &&
            std::all_of(runs.begin() + static_cast<long>(r) + 1, runs.begin() + static_cast<long>(r) + 6,
                        [](std::string_view s) { return s.size() == 2; })) {
            const int64_t t = utcSeconds(number(a), number(runs[r + 1]), number(runs[r + 2]),
                                         number(runs[r + 3]), number(runs[r + 4]), number(runs[r + 5]));
            if (t >= 0) {
                return t;
            }
        }
    }
    return -1;
}

int64_t timeFromExif(std::string_view text) {
    // The Orus L writes ISO 8601 with its UTC offset: 2026-10-19T11:53:14-0600.
    const bool iso = text.size() >= 19 && text[4] == '-' && text[7] == '-' && text[10] == 'T';
    if (text.size() < 19 || (!iso && (text[4] != ':' || text[7] != ':' || text[10] != ' ')) ||
        text[13] != ':' || text[16] != ':') {
        return -1;
    }
    for (const size_t i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18}) {
        if (!isDigit(text[i])) {
            return -1;
        }
    }
    const int64_t local = utcSeconds(number(text.substr(0, 4)), number(text.substr(5, 2)),
                                     number(text.substr(8, 2)), number(text.substr(11, 2)),
                                     number(text.substr(14, 2)), number(text.substr(17, 2)));
    if (!iso || local < 0 || text.size() < 20 || text[19] == 'Z' || text[19] == '\0') {
        return local;
    }
    // "+HHMM", "+HH:MM" or "+HH"
    std::string_view zone = text.substr(19);
    if ((zone[0] != '+' && zone[0] != '-') || zone.size() < 3 || !isDigit(zone[1]) || !isDigit(zone[2])) {
        return local;
    }
    int minutes = number(zone.substr(1, 2)) * 60;
    const std::string_view rest = zone.substr(3);
    const size_t m = !rest.empty() && rest[0] == ':' ? 1 : 0;
    if (rest.size() >= m + 2 && isDigit(rest[m]) && isDigit(rest[m + 1])) {
        minutes += number(rest.substr(m, 2));
    }
    return local - (zone[0] == '+' ? 1 : -1) * static_cast<int64_t>(minutes) * 60;
}

int64_t timeFromHttpDate(std::string_view text) {
    // "Sun, 19 Oct 2026 11:02:33 GMT"
    static const char* months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                   "jul", "aug", "sep", "oct", "nov", "dec"};
    const size_t comma = text.find(',');
    std::string_view rest = comma == std::string_view::npos ? text : text.substr(comma + 1);
    while (!rest.empty() && rest.front() == ' ') {
        rest.remove_prefix(1);
    }
    if (rest.size() < 20 || rest[2] != ' ' || rest[6] != ' ' || rest[11] != ' ' || rest[14] != ':' ||
        rest[17] != ':') {
        return -1;
    }
    int month = 0;
    const std::string m = lower(rest.substr(3, 3));
    for (int i = 0; i < 12; ++i) {
        if (m == months[i]) {
            month = i + 1;
        }
    }
    for (const size_t i : {0, 1, 7, 8, 9, 10, 12, 13, 15, 16, 18, 19}) {
        if (!isDigit(rest[i])) {
            return -1;
        }
    }
    if (month == 0) {
        return -1;
    }
    return utcSeconds(number(rest.substr(7, 4)), month, number(rest.substr(0, 2)),
                      number(rest.substr(12, 2)), number(rest.substr(15, 2)), number(rest.substr(18, 2)));
}

std::string pathEncode(std::string_view name) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : name) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hexValue(text[i + 1]);
            const int lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

JpegExif readJpegExif(const uint8_t* data, size_t size) {
    JpegExif out;
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        out.needed = size < 4 ? 4 : 0;
        return out;
    }
    out.isJpeg = true;
    size_t at = 2;
    while (true) {
        if (at + 4 > size) {
            out.needed = at + 4;
            return out;
        }
        if (data[at] != 0xFF) {
            return out; // not a marker: give up quietly
        }
        const uint8_t marker = data[at + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            at += 2;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            return out; // image data: no EXIF before it
        }
        const size_t length = (static_cast<size_t>(data[at + 2]) << 8) | data[at + 3];
        const size_t segment = at + 4;
        const size_t end = at + 2 + length;
        if (marker == 0xE1 && length >= 8) {
            if (end > size) {
                out.needed = end;
                return out;
            }
            if (std::memcmp(data + segment, "Exif\0\0", 6) != 0) {
                at = end;
                continue;
            }
            const uint8_t* tiff = data + segment + 6;
            const size_t tiffSize = end - (segment + 6);
            if (tiffSize < 8) {
                return out;
            }
            const bool little = tiff[0] == 'I' && tiff[1] == 'I';
            if (!little && !(tiff[0] == 'M' && tiff[1] == 'M')) {
                return out;
            }
            auto u16 = [&](size_t o) -> uint32_t {
                if (o + 2 > tiffSize) {
                    return 0;
                }
                return little ? tiff[o] | (tiff[o + 1] << 8) : (tiff[o] << 8) | tiff[o + 1];
            };
            auto u32 = [&](size_t o) -> uint32_t {
                if (o + 4 > tiffSize) {
                    return 0;
                }
                return little ? tiff[o] | (tiff[o + 1] << 8) | (tiff[o + 2] << 16) |
                                    (static_cast<uint32_t>(tiff[o + 3]) << 24)
                              : (static_cast<uint32_t>(tiff[o]) << 24) | (tiff[o + 1] << 16) |
                                    (tiff[o + 2] << 8) | tiff[o + 3];
            };
            auto ascii = [&](size_t entry) -> std::string_view {
                const uint32_t count = u32(entry + 4);
                const size_t offset = count <= 4 ? entry + 8 : u32(entry + 8);
                if (count == 0 || offset + count > tiffSize) {
                    return {};
                }
                return {reinterpret_cast<const char*>(tiff + offset), count};
            };
            int64_t dateTime = -1;
            int64_t original = -1;
            uint32_t exifIfd = 0;
            const uint32_t ifd0 = u32(4);
            const uint32_t count0 = u16(ifd0);
            for (uint32_t i = 0; i < count0 && count0 < 512; ++i) {
                const size_t entry = ifd0 + 2 + 12 * i;
                const uint32_t tag = u16(entry);
                if (tag == 0x8769) {
                    exifIfd = u32(entry + 8);
                } else if (tag == 0x0132) {
                    dateTime = timeFromExif(ascii(entry));
                }
            }
            if (exifIfd != 0) {
                const uint32_t count = u16(exifIfd);
                for (uint32_t i = 0; i < count && count < 512; ++i) {
                    const size_t entry = exifIfd + 2 + 12 * i;
                    if (u16(entry) == 0x9003) {
                        original = timeFromExif(ascii(entry));
                    }
                }
            }
            out.captureTime = original >= 0 ? original : dateTime;
            const uint32_t ifd1 = u32(ifd0 + 2 + 12 * count0);
            if (ifd1 != 0) {
                uint32_t offset = 0;
                uint32_t thumbLength = 0;
                const uint32_t count1 = u16(ifd1);
                for (uint32_t i = 0; i < count1 && count1 < 512; ++i) {
                    const size_t entry = ifd1 + 2 + 12 * i;
                    if (u16(entry) == 0x0201) {
                        offset = u32(entry + 8);
                    } else if (u16(entry) == 0x0202) {
                        thumbLength = u32(entry + 8);
                    }
                }
                if (offset != 0 && thumbLength > 4 && offset + thumbLength <= tiffSize &&
                    tiff[offset] == 0xFF && tiff[offset + 1] == 0xD8) {
                    out.hasThumbnail = true;
                    out.thumbnailOffset = segment + 6 + offset;
                    out.thumbnailLength = thumbLength;
                }
            }
            return out;
        }
        at = end;
    }
}

std::vector<uint32_t> groupIntoFlights(const std::vector<int64_t>& times, int64_t gapSeconds) {
    std::vector<uint32_t> flights(times.size(), 0);
    uint32_t flight = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        if (i > 0) {
            const bool unknownStarts = times[i] < 0 && times[i - 1] >= 0;
            const bool gap = times[i] >= 0 && times[i - 1] - times[i] > gapSeconds;
            if (unknownStarts || gap) {
                ++flight;
            }
        }
        flights[i] = flight;
    }
    return flights;
}

} // namespace imago::web
