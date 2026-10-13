#include "rtsp/sdp.h"

#include "util/base64.h"
#include "util/text.h"

namespace imago {

namespace {

void decodeInto(std::vector<uint8_t>& slot, std::string_view b64) {
    if (auto bytes = base64Decode(text::trim(b64))) {
        slot = std::move(*bytes);
    }
}

void applyFmtp(SdpVideo& video, std::string_view params) {
    for (std::string_view item : text::split(params, ';')) {
        item = text::trim(item);
        const size_t eq = item.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = text::trim(item.substr(0, eq));
        const std::string_view value = text::trim(item.substr(eq + 1));
        if (text::iequals(key, "sprop-parameter-sets")) {
            // H.264: comma-separated SPS, PPS (extra PPS are ignored)
            for (std::string_view set : text::split(value, ',')) {
                std::vector<uint8_t> nal;
                decodeInto(nal, set);
                if (nal.empty()) {
                    continue;
                }
                const uint8_t type = nal[0] & 0x1F;
                if (type == 7 && video.parameterSets.sps.empty()) {
                    video.parameterSets.sps = std::move(nal);
                } else if (type == 8 && video.parameterSets.pps.empty()) {
                    video.parameterSets.pps = std::move(nal);
                }
            }
        } else if (text::iequals(key, "sprop-vps")) {
            decodeInto(video.parameterSets.vps, value);
        } else if (text::iequals(key, "sprop-sps")) {
            decodeInto(video.parameterSets.sps, value);
        } else if (text::iequals(key, "sprop-pps")) {
            decodeInto(video.parameterSets.pps, value);
        }
    }
}

} // namespace

std::optional<SdpVideo> parseSdpVideo(std::string_view sdp) {
    std::optional<SdpVideo> video;
    bool inVideo = false;
    std::string payloadTypeText;

    for (std::string_view line : text::split(sdp, '\n')) {
        line = text::trim(line);
        if (line.size() < 2 || line[1] != '=') {
            continue;
        }
        const char kind = line[0];
        const std::string_view value = line.substr(2);

        if (kind == 'm') {
            if (video && video->codec != IMAGO_CODEC_UNKNOWN) {
                break; // first usable video stream only
            }
            inVideo = text::istartsWith(value, "video ");
            if (inVideo) {
                video = SdpVideo{};
                // m=video <port> RTP/AVP <pt> [<pt>...]
                const auto parts = text::split(value, ' ');
                payloadTypeText = parts.size() >= 4 ? std::string(parts[3]) : "96";
                if (auto pt = text::parseInt(payloadTypeText)) {
                    video->payloadType = static_cast<uint8_t>(*pt);
                }
            }
            continue;
        }
        if (!inVideo || kind != 'a') {
            continue;
        }
        if (text::istartsWith(value, "control:")) {
            video->control = std::string(text::trim(value.substr(8)));
        } else if (text::istartsWith(value, "rtpmap:")) {
            // a=rtpmap:<pt> <encoding>/<clock>
            const std::string_view rest = value.substr(7);
            const size_t space = rest.find(' ');
            if (space == std::string_view::npos || rest.substr(0, space) != payloadTypeText) {
                continue;
            }
            const auto encoding = text::split(rest.substr(space + 1), '/');
            if (text::iequals(encoding[0], "H264")) {
                video->codec = IMAGO_CODEC_H264;
            } else if (text::iequals(encoding[0], "H265") || text::iequals(encoding[0], "HEVC")) {
                video->codec = IMAGO_CODEC_H265;
            }
            if (encoding.size() > 1) {
                if (auto rate = text::parseInt(encoding[1])) {
                    video->clockRate = static_cast<uint32_t>(*rate);
                }
            }
        } else if (text::istartsWith(value, "fmtp:")) {
            const std::string_view rest = value.substr(5);
            const size_t space = rest.find(' ');
            if (space == std::string_view::npos || rest.substr(0, space) != payloadTypeText) {
                continue;
            }
            applyFmtp(*video, rest.substr(space + 1));
        }
    }
    if (!video || video->codec == IMAGO_CODEC_UNKNOWN) {
        return std::nullopt;
    }
    return video;
}

} // namespace imago
