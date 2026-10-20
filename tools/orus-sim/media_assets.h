#pragma once

#include <cstddef>

namespace imago::sim {

// The photos in testdata/media (made by testdata/make_media.py), built into the simulator.
struct MediaAsset {
    const char* name;
    const unsigned char* data;
    size_t size;
};
extern const MediaAsset kMediaAssets[];
extern const size_t kMediaAssetCount;

} // namespace imago::sim
