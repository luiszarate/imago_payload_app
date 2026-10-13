#include "rtp/depacketizer.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace imago {

namespace {

constexpr size_t kPrefixSize = 4;
constexpr size_t kInitialCapacity = 512 * 1024;
// A sequence jump this large means the sender restarted, not that packets were lost.
constexpr int kResyncDistance = 3000;

namespace h264 {
constexpr uint8_t kIdr = 5;
constexpr uint8_t kSps = 7;
constexpr uint8_t kPps = 8;
constexpr uint8_t kAud = 9;
constexpr uint8_t kStapA = 24;
constexpr uint8_t kFuA = 28;
inline uint8_t type(const uint8_t* nal) { return nal[0] & 0x1F; }
} // namespace h264

namespace h265 {
constexpr uint8_t kIrapFirst = 16;
constexpr uint8_t kIrapLast = 21;
constexpr uint8_t kVps = 32;
constexpr uint8_t kSps = 33;
constexpr uint8_t kPps = 34;
constexpr uint8_t kAud = 35;
constexpr uint8_t kAp = 48;
constexpr uint8_t kFu = 49;
inline uint8_t type(const uint8_t* nal) { return (nal[0] >> 1) & 0x3F; }
} // namespace h265

} // namespace

Depacketizer::Depacketizer(imago_codec codec, imago_nal_format format)
    : codec_(codec), format_(format) {
    frame_.reserve(kInitialCapacity);
}

void Depacketizer::setParameterSets(const ParameterSets& sets) {
    if (!sets.vps.empty()) {
        storeParameterSet(params_.vps, sets.vps.data(), sets.vps.size());
    }
    if (!sets.sps.empty()) {
        storeParameterSet(params_.sps, sets.sps.data(), sets.sps.size());
    }
    if (!sets.pps.empty()) {
        storeParameterSet(params_.pps, sets.pps.data(), sets.pps.size());
    }
}

void Depacketizer::reset() {
    resetFrame();
    sequenceKnown_ = false;
    lossBeforeNextFrame_ = false;
    waitingForKeyframe_ = true;
    waitStartNs_ = 0;
    transitKnown_ = false;
}

void Depacketizer::resetFrame() {
    frame_.clear();
    frameActive_ = false;
    frameCorrupt_ = false;
    frameKeyframe_ = false;
    fragmentActive_ = false;
}

bool Depacketizer::haveParameterSets() const {
    if (params_.sps.empty() || params_.pps.empty()) {
        return false;
    }
    return codec_ != IMAGO_CODEC_H265 || !params_.vps.empty();
}

void Depacketizer::markLoss() {
    if (frameActive_) {
        frameCorrupt_ = true;
        abortFragment();
    } else {
        lossBeforeNextFrame_ = true;
    }
}

void Depacketizer::push(const RtpPacket& packet, int64_t nowNs, const FrameSink& sink) {
    ++stats_.packetsReceived;
    if (waitStartNs_ == 0) {
        waitStartNs_ = nowNs;
    }

    // Sequence numbers: detect loss, drop late or duplicate packets.
    if (!sequenceKnown_) {
        sequenceKnown_ = true;
    } else {
        const auto distance = static_cast<int16_t>(packet.sequence - expectedSequence_);
        if (std::abs(distance) > kResyncDistance) {
            resetFrame();
            transitKnown_ = false;
        } else if (distance < 0) {
            ++stats_.packetsLate;
            return;
        } else if (distance > 0) {
            stats_.packetsLost += static_cast<uint32_t>(distance);
            // The missing packets may also have carried the previous frame's marker.
            if (frameActive_ && packet.timestamp != frameTimestamp_) {
                frameCorrupt_ = true;
                deliver(nowNs, sink);
                lossBeforeNextFrame_ = true;
            } else {
                markLoss();
            }
        }
    }
    expectedSequence_ = static_cast<uint16_t>(packet.sequence + 1);

    // Interarrival jitter (RFC 3550 section 6.4.1) in 90 kHz ticks.
    const double arrivalTicks = static_cast<double>(nowNs) * 90000.0 / 1e9;
    const double transit = arrivalTicks - static_cast<double>(packet.timestamp);
    if (transitKnown_) {
        const double d = std::fabs(transit - lastTransit_);
        // Ignore the 32-bit timestamp wrap and sender restarts.
        if (d < 90000.0 * 10) {
            jitterTicks_ += (d - jitterTicks_) / 16.0;
            stats_.jitterMs = static_cast<float>(jitterTicks_ / 90.0);
        }
    }
    transitKnown_ = true;
    lastTransit_ = transit;

    // A new timestamp starts a new frame; deliver the previous one if its marker never came.
    if (frameActive_ && packet.timestamp != frameTimestamp_) {
        deliver(nowNs, sink);
    }
    if (!frameActive_) {
        frameActive_ = true;
        frameTimestamp_ = packet.timestamp;
        frameFirstNs_ = nowNs;
        frameCorrupt_ = lossBeforeNextFrame_;
        lossBeforeNextFrame_ = false;
    }
    frameLastNs_ = nowNs;

    if (packet.payloadSize > 0) {
        if (codec_ == IMAGO_CODEC_H265) {
            parseH265(packet.payload, packet.payloadSize);
        } else {
            parseH264(packet.payload, packet.payloadSize);
        }
    }

    if (packet.marker) {
        markerSeen_ = true;
        deliver(nowNs, sink);
    }
}

// ----------------------------------------------------------------------------------------------
// Payload formats

void Depacketizer::parseH264(const uint8_t* payload, size_t size) {
    const uint8_t type = h264::type(payload);
    if (type >= 1 && type <= 23) {
        appendNal(payload, size);
        return;
    }
    if (type == h264::kStapA) {
        size_t offset = 1;
        while (offset + 2 <= size) {
            const size_t nalSize = static_cast<size_t>((payload[offset] << 8) | payload[offset + 1]);
            offset += 2;
            if (nalSize == 0 || offset + nalSize > size) {
                frameCorrupt_ = true;
                return;
            }
            appendNal(payload + offset, nalSize);
            offset += nalSize;
        }
        return;
    }
    if (type == h264::kFuA) {
        if (size < 2) {
            frameCorrupt_ = true;
            return;
        }
        const uint8_t fuHeader = payload[1];
        const bool start = (fuHeader & 0x80) != 0;
        const bool end = (fuHeader & 0x40) != 0;
        if (start) {
            if (fragmentActive_) {
                abortFragment(); // previous fragment never ended
                frameCorrupt_ = true;
            }
            const uint8_t header = static_cast<uint8_t>((payload[0] & 0xE0) | (fuHeader & 0x1F));
            beginFragment(&header, 1);
        } else if (!fragmentActive_) {
            frameCorrupt_ = true; // start fragment was lost
            return;
        }
        appendFragment(payload + 2, size - 2);
        if (end) {
            endFragment();
        }
        return;
    }
    // STAP-B, MTAP and FU-B are not used in non-interleaved mode.
    frameCorrupt_ = true;
}

void Depacketizer::parseH265(const uint8_t* payload, size_t size) {
    if (size < 2) {
        frameCorrupt_ = true;
        return;
    }
    const uint8_t type = h265::type(payload);
    if (type < h265::kAp) {
        appendNal(payload, size);
        return;
    }
    if (type == h265::kAp) {
        size_t offset = 2;
        while (offset + 2 <= size) {
            const size_t nalSize = static_cast<size_t>((payload[offset] << 8) | payload[offset + 1]);
            offset += 2;
            if (nalSize < 2 || offset + nalSize > size) {
                frameCorrupt_ = true;
                return;
            }
            appendNal(payload + offset, nalSize);
            offset += nalSize;
        }
        return;
    }
    if (type == h265::kFu) {
        if (size < 3) {
            frameCorrupt_ = true;
            return;
        }
        const uint8_t fuHeader = payload[2];
        const bool start = (fuHeader & 0x80) != 0;
        const bool end = (fuHeader & 0x40) != 0;
        if (start) {
            if (fragmentActive_) {
                abortFragment();
                frameCorrupt_ = true;
            }
            const uint8_t header[2] = {
                static_cast<uint8_t>((payload[0] & 0x81) | ((fuHeader & 0x3F) << 1)), payload[1]};
            beginFragment(header, 2);
        } else if (!fragmentActive_) {
            frameCorrupt_ = true;
            return;
        }
        appendFragment(payload + 3, size - 3);
        if (end) {
            endFragment();
        }
        return;
    }
    // PACI (50) and reserved types carry nothing we decode.
}

// ----------------------------------------------------------------------------------------------
// Access unit assembly

bool Depacketizer::acceptNal(const uint8_t* nal, size_t size) {
    if (size == 0) {
        return false;
    }
    if (codec_ == IMAGO_CODEC_H265) {
        const uint8_t type = h265::type(nal);
        switch (type) {
        case h265::kVps:
            storeParameterSet(params_.vps, nal, size);
            return false;
        case h265::kSps:
            storeParameterSet(params_.sps, nal, size);
            return false;
        case h265::kPps:
            storeParameterSet(params_.pps, nal, size);
            return false;
        case h265::kAud:
            return false;
        default:
            if (type >= h265::kIrapFirst && type <= h265::kIrapLast) {
                frameKeyframe_ = true;
            }
            return true;
        }
    }
    switch (h264::type(nal)) {
    case h264::kSps:
        storeParameterSet(params_.sps, nal, size);
        return false;
    case h264::kPps:
        storeParameterSet(params_.pps, nal, size);
        return false;
    case h264::kAud:
        return false;
    case h264::kIdr:
        frameKeyframe_ = true;
        return true;
    default:
        return true;
    }
}

void Depacketizer::storeParameterSet(std::vector<uint8_t>& slot, const uint8_t* nal, size_t size) {
    if (slot.size() == size && std::memcmp(slot.data(), nal, size) == 0) {
        return;
    }
    slot.assign(nal, nal + size);
    parameterSetsChanged_ = true;
}

void Depacketizer::writePrefix(size_t offset, size_t nalSize) {
    uint8_t* p = frame_.data() + offset;
    if (format_ == IMAGO_NAL_ANNEXB) {
        p[0] = 0;
        p[1] = 0;
        p[2] = 0;
        p[3] = 1;
    } else {
        p[0] = static_cast<uint8_t>(nalSize >> 24);
        p[1] = static_cast<uint8_t>(nalSize >> 16);
        p[2] = static_cast<uint8_t>(nalSize >> 8);
        p[3] = static_cast<uint8_t>(nalSize);
    }
}

void Depacketizer::appendNal(const uint8_t* nal, size_t size) {
    if (!acceptNal(nal, size)) {
        return;
    }
    const size_t offset = frame_.size();
    frame_.resize(offset + kPrefixSize + size);
    writePrefix(offset, size);
    std::memcpy(frame_.data() + offset + kPrefixSize, nal, size);
}

void Depacketizer::beginFragment(const uint8_t* header, size_t headerSize) {
    fragmentActive_ = true;
    fragmentOffset_ = frame_.size();
    frame_.resize(fragmentOffset_ + kPrefixSize);
    frame_.insert(frame_.end(), header, header + headerSize);
}

void Depacketizer::appendFragment(const uint8_t* data, size_t size) {
    frame_.insert(frame_.end(), data, data + size);
}

void Depacketizer::endFragment() {
    fragmentActive_ = false;
    const size_t nalOffset = fragmentOffset_ + kPrefixSize;
    const size_t nalSize = frame_.size() - nalOffset;
    if (!acceptNal(frame_.data() + nalOffset, nalSize)) {
        frame_.resize(fragmentOffset_);
        return;
    }
    writePrefix(fragmentOffset_, nalSize);
}

void Depacketizer::abortFragment() {
    if (fragmentActive_) {
        frame_.resize(fragmentOffset_);
        fragmentActive_ = false;
    }
}

void Depacketizer::deliver(int64_t nowNs, const FrameSink& sink) {
    if (fragmentActive_) {
        abortFragment();
        frameCorrupt_ = true;
    }
    const bool empty = frame_.empty();
    const bool keyframe = frameKeyframe_;
    if (empty) {
        // Nothing decodable: either only parameter sets arrived, or loss removed everything.
        // In the second case the next frame references a missing picture.
        if (frameCorrupt_) {
            ++stats_.framesDropped;
            lossBeforeNextFrame_ = true;
        }
        resetFrame();
        return;
    }
    if (!haveParameterSets()) {
        ++stats_.framesDropped;
        resetFrame();
        return;
    }
    if (waitingForKeyframe_) {
        if (keyframe || nowNs - waitStartNs_ > kKeyframeWaitNs) {
            waitingForKeyframe_ = false;
        } else {
            ++stats_.framesDropped;
            resetFrame();
            return;
        }
    }

    AssembledFrame out;
    out.data = frame_.data();
    out.size = frame_.size();
    out.keyframe = keyframe;
    out.corrupt = frameCorrupt_;
    out.parameterSetsChanged = parameterSetsChanged_;
    out.rtpTimestamp = frameTimestamp_;
    out.frameNumber = frameNumber_++;
    out.firstPacketNs = frameFirstNs_;
    out.lastPacketNs = frameLastNs_;

    ++stats_.framesDelivered;
    if (frameCorrupt_) {
        ++stats_.framesCorrupt;
    }
    parameterSetsChanged_ = false;
    sink(out);
    resetFrame();
}

} // namespace imago
