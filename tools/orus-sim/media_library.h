#pragma once

// The simulator's media storage: three invented flights of photos and videos, as an Orus L's
// web server lists them (docs/protocol/orus-l.md, "Media files"). File names follow a pattern
// of our own (EO_/IR_/VID_ + date and time); the real Orus L's naming is not known yet.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace imago::sim {

class MediaLibrary {
  public:
    struct Item {
        std::string name;
        const unsigned char* data = nullptr; // nullptr: synthetic bytes (videos)
        size_t size = 0;
        int64_t modified = 0; // UTC seconds
        bool video = false;
    };

    MediaLibrary(); // the default storage
    std::vector<Item> list() const;
    std::optional<Item> find(const std::string& name) const;
    bool remove(const std::string& name);
    size_t removeAll(bool videos);
    size_t count() const;
    // Copies bytes [offset, offset + size) of item into out.
    static void read(const Item& item, size_t offset, char* out, size_t size);

  private:
    mutable std::mutex mutex_;
    std::vector<Item> items_;
};

} // namespace imago::sim
