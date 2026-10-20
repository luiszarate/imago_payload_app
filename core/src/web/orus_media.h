#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace imago::web {

// The Orus L's media files on its web server (port 8000), as Gremsy's SDK describes the
// server (docs/protocol/orus-l.md, "Media files"; not yet checked on our Orus L):
// GET /list-file is an HTML page with one "/delete/<name>" link per file, GET /download/<name>
// serves the file, GET /delete/<name> deletes it and redirects back to the list.
inline constexpr const char* kMediaListPath = "/list-file";
inline constexpr const char* kMediaDownloadPrefix = "/download/";
inline constexpr const char* kMediaDeletePrefix = "/delete/";
// Payloads with an internal memory and an SD card: POST {"option": "<value>"} as JSON.
inline constexpr const char* kMediaStoragePath = "/process-option";

enum class MediaKind { Photo, Video, Radiometric, Other };

struct MediaStorage {
    std::string value;
    std::string label;
};

struct MediaListing {
    // The page is the payload's login form instead of the list (some Gremsy payloads).
    bool loginRequired = false;
    std::vector<std::string> names; // decoded, in page order, no duplicates
    std::vector<MediaStorage> storages; // empty with a single storage
    int currentStorage = -1;
};

MediaListing parseMediaListing(const std::string& html);

// From the file name's extension. TIFF is the IR camera's radiometric picture.
MediaKind mediaKindOf(std::string_view name);
// An IR camera photo, as far as the name tells ("IR" as its own word) *(unverified)*.
bool isThermalName(std::string_view name);

// A date and time written in the name (20261019_110233, 2026-10-19_11-02-33,
// 20261019110233...), read as UTC seconds since 1970; -1 if none.
int64_t timeFromName(std::string_view name);
// "YYYY:MM:DD HH:MM:SS" (EXIF) as UTC seconds; -1 if malformed.
int64_t timeFromExif(std::string_view text);
// An HTTP date (Last-Modified, RFC 7231 IMF-fixdate) as UTC seconds; -1 if malformed.
int64_t timeFromHttpDate(std::string_view text);

// Percent-encodes a file name for a URL path (spaces as %20, as the payload's links do).
std::string pathEncode(std::string_view name);
// Undoes %XX escapes ('+' stays a plus: these are paths, not form fields).
std::string percentDecode(std::string_view text);

// What the start of a JPEG tells: its EXIF thumbnail (a small JPEG inside the APP1
// segment) and capture time. needed > size means the segments run past the bytes given:
// read at least `needed` bytes and look again.
struct JpegExif {
    bool isJpeg = false;
    bool hasThumbnail = false;
    size_t thumbnailOffset = 0; // from the start of the file
    size_t thumbnailLength = 0;
    int64_t captureTime = -1;   // DateTimeOriginal, else DateTime; UTC seconds
    size_t needed = 0;
};
JpegExif readJpegExif(const uint8_t* data, size_t size);

// Groups capture times into flights: newest first, a new flight after a gap longer than
// gapSeconds. times must be sorted newest first; unknown times (-1) at the end form their
// own group. Returns the flight index of each time.
std::vector<uint32_t> groupIntoFlights(const std::vector<int64_t>& times, int64_t gapSeconds);

} // namespace imago::web
