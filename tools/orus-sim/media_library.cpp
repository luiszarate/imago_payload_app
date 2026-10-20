#include "media_library.h"

#include "media_assets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace imago::sim {

namespace {

const MediaAsset* asset(const char* name) {
    for (size_t i = 0; i < kMediaAssetCount; ++i) {
        if (std::strcmp(kMediaAssets[i].name, name) == 0) {
            return &kMediaAssets[i];
        }
    }
    return nullptr;
}

// 2026-10-16 00:00:00 UTC plus days, hours, minutes and seconds.
int64_t at(int day, int hour, int minute, int second) {
    return 1792108800 + (day - 16) * 86400 + hour * 3600 + minute * 60 + second;
}

std::string stamp(int64_t t) {
    // Days since 1970 to a civil date (Howard Hinnant's civil_from_days).
    const int64_t z = t / 86400 + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    const int64_t s = t % 86400;
    char text[64];
    std::snprintf(text, sizeof(text), "%04lld%02u%02u_%02lld%02lld%02lld", static_cast<long long>(y), m, d,
                  static_cast<long long>(s / 3600), static_cast<long long>(s / 60 % 60),
                  static_cast<long long>(s % 60));
    return text;
}

} // namespace

MediaLibrary::MediaLibrary() {
    auto photo = [&](const char* prefix, const char* source, const char* ext, int64_t t) {
        const MediaAsset* a = asset(source);
        if (a != nullptr) {
            items_.push_back({std::string(prefix) + stamp(t) + ext, a->data, a->size, t, false});
        }
    };
    auto video = [&](int64_t t, size_t megabytes, int seconds) {
        // The file is closed, and dated, when the recording ends.
        items_.push_back({"VID_" + stamp(t) + ".mp4", nullptr, megabytes * 1000 * 1000, t + seconds, true});
    };
    struct Flight {
        int64_t start;
        int eo, ir, tiff;
        std::vector<std::pair<size_t, int>> videos; // MB, seconds
    };
    const Flight flights[] = {
        {at(19, 11, 2, 33), 16, 4, 2, {{48, 240}, {12, 60}}},
        {at(19, 9, 40, 10), 10, 0, 0, {{30, 150}, {8, 40}, {64, 320}}},
        {at(16, 15, 12, 48), 6, 4, 0, {{20, 100}}},
    };
    int n = 0;
    for (const Flight& f : flights) {
        int64_t t = f.start;
        for (int i = 0; i < f.eo; ++i, t += 70 + (i * 37) % 60) {
            const char* sources[] = {"eo1.jpg", "eo2.jpg", "eo3.jpg"};
            photo("EO_", sources[n++ % 3], ".jpg", t);
            if (i < f.ir) {
                photo("IR_", "ir.jpg", ".jpg", t + 1);
            }
            if (i < f.tiff) {
                photo("IR_", "ir.tiff", ".tiff", t + 1);
            }
            if (i == 3 && !f.videos.empty()) {
                for (const auto& [mb, seconds] : f.videos) {
                    video(t + 20, mb, seconds);
                    t += seconds + 30;
                }
            }
        }
    }
    // A file named by hand, with a space and no date in its name.
    if (const MediaAsset* a = asset("eo3.jpg")) {
        items_.push_back({"calibration shot.jpg", a->data, a->size, at(12, 8, 30, 0), false});
    }
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
}

std::vector<MediaLibrary::Item> MediaLibrary::list() const {
    std::lock_guard lock(mutex_);
    return items_;
}

std::optional<MediaLibrary::Item> MediaLibrary::find(const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (const Item& item : items_) {
        if (item.name == name) {
            return item;
        }
    }
    return std::nullopt;
}

bool MediaLibrary::remove(const std::string& name) {
    std::lock_guard lock(mutex_);
    const auto before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Item& i) { return i.name == name; }),
                 items_.end());
    return items_.size() != before;
}

size_t MediaLibrary::removeAll(bool videos) {
    std::lock_guard lock(mutex_);
    const auto before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Item& i) { return i.video == videos; }),
                 items_.end());
    return before - items_.size();
}

size_t MediaLibrary::count() const {
    std::lock_guard lock(mutex_);
    return items_.size();
}

void MediaLibrary::read(const Item& item, size_t offset, char* out, size_t size) {
    if (item.data != nullptr) {
        std::memcpy(out, item.data + offset, size);
        return;
    }
    // A video: an MP4 file type box, then filler.
    static const unsigned char ftyp[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm',
                                         0, 0, 2,  0, 'i', 's', 'o', 'm', 'm', 'p', '4', '1'};
    for (size_t i = 0; i < size; ++i) {
        const size_t p = offset + i;
        out[i] = static_cast<char>(p < sizeof(ftyp) ? ftyp[p] : (p * 31 + p / 4096) & 0xFF);
    }
}

} // namespace imago::sim
