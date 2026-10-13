#pragma once

#include "imago/imago_core.h"
#include "rtp/depacketizer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace imago {

struct SdpVideo {
    imago_codec codec = IMAGO_CODEC_UNKNOWN;
    uint8_t payloadType = 96;
    uint32_t clockRate = 90000;
    std::string control;     // media-level a=control, may be relative
    ParameterSets parameterSets; // from sprop-parameter-sets / sprop-vps,sps,pps
};

// Returns the first H.264 or H.265 video media description.
std::optional<SdpVideo> parseSdpVideo(std::string_view sdp);

} // namespace imago
