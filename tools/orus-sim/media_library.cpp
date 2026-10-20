#include "media_library.h"

#include "media_assets.h"

#include <algorithm>
#include <cstring>

namespace imago::sim {

namespace {

const MediaAsset* asset(const std::string& name) {
    for (size_t i = 0; i < kMediaAssetCount; ++i) {
        if (name == kMediaAssets[i].name) {
            return &kMediaAssets[i];
        }
    }
    return nullptr;
}

// 2026-10-16 00:00:00 UTC plus days, hours, minutes and seconds.
int64_t at(int day, int hour, int minute, int second) {
    return 1792108800 + (day - 16) * 86400 + hour * 3600 + minute * 60 + second;
}

} // namespace

MediaLibrary::MediaLibrary() {
    // One shutter press makes IMG_EO_<n>.jpeg and IMG_IR_<n>.jpeg with the same date; a
    // recording, VID_EO_<n>.mp4 and VID_IR_<n>.mp4. EO photos are about 10 MB (3840 x 2160 on
    // the payload; our picture plus filler), IR ones under 1 MB. The numbers do not follow the
    // dates: the 2026-10-16 flight was numbered after the 10-19 ones, as seen on our Orus L.
    struct Flight {
        int64_t start;
        int number;  // of the first photo
        int photos;
        int video;   // number of the first recording
        std::vector<std::pair<size_t, int>> videos; // EO MB, seconds
    };
    const Flight flights[] = {
        {at(19, 11, 2, 33), 159, 16, 28, {{186, 173}, {12, 11}}},
        {at(19, 9, 40, 10), 175, 10, 30, {{60, 56}, {20, 18}, {120, 110}}},
        {at(16, 15, 12, 48), 206, 6, 33, {{40, 37}}},
    };
    int scene = 0;
    for (const Flight& f : flights) {
        int64_t t = f.start;
        int video = f.video;
        for (int i = 0; i < f.photos; ++i, t += 70 + (i * 37) % 60) {
            const std::string source = std::to_string(scene++ % 3 + 1) + ".jpg";
            const std::string number = std::to_string(f.number + i);
            if (const MediaAsset* eo = asset("eo" + source)) {
                const size_t size = 9000000 + static_cast<size_t>(i % 5) * 450000;
                items_.push_back({"IMG_EO_" + number + ".jpeg", eo->data, eo->size, size, t, false});
            }
            if (const MediaAsset* ir = asset("ir" + source)) {
                items_.push_back({"IMG_IR_" + number + ".jpeg", ir->data, ir->size, ir->size, t, false});
            }
            if (i == 3) {
                for (const auto& [mb, seconds] : f.videos) {
                    // The file is closed, and dated, when the recording ends.
                    const int64_t end = t + 20 + seconds;
                    const std::string n = std::to_string(video++);
                    items_.push_back({"VID_EO_" + n + ".mp4", nullptr, 0, mb * 1000 * 1000, end, true});
                    items_.push_back({"VID_IR_" + n + ".mp4", nullptr, 0, mb * 450 * 1000, end, true});
                    t += seconds + 30;
                }
            }
        }
    }
    // As the payload lists them: newest first, IR before EO within a shot.
    std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
        return a.modified != b.modified ? a.modified > b.modified : a.name > b.name;
    });
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
    // The picture, then filler (after the JPEG's end marker, which decoders stop at). A video:
    // an MP4 file type box, then filler.
    static const unsigned char ftyp[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'm', 'p', '4', '2',
                                         0, 0, 0,  0, 'm', 'p', '4', '2', 'i', 's', 'o', 'm'};
    for (size_t i = 0; i < size; ++i) {
        const size_t p = offset + i;
        if (item.data != nullptr && p < item.dataSize) {
            out[i] = static_cast<char>(item.data[p]);
        } else if (item.video && p < sizeof(ftyp)) {
            out[i] = static_cast<char>(ftyp[p]);
        } else {
            out[i] = static_cast<char>((p * 31 + p / 4096) & 0xFF);
        }
    }
}

} // namespace imago::sim
