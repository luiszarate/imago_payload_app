#pragma once

#include "imago/imago_core.h"
#include "rtp/rtp_packet.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace imago {

// One access unit ready for decoding. Pointers are valid only during the sink call.
struct AssembledFrame {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool keyframe = false;
    bool corrupt = false;
    bool parameterSetsChanged = false;
    uint32_t rtpTimestamp = 0;
    uint32_t frameNumber = 0;
    int64_t firstPacketNs = 0;
    int64_t lastPacketNs = 0;
};

struct ParameterSets {
    std::vector<uint8_t> vps; // H.265 only
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
};

struct DepacketizerStats {
    uint32_t packetsReceived = 0;
    uint32_t packetsLost = 0;
    uint32_t packetsLate = 0;
    uint32_t framesDelivered = 0;
    uint32_t framesCorrupt = 0;
    uint32_t framesDropped = 0; // waiting for parameter sets or the first keyframe
    float jitterMs = 0;
};

// Rebuilds H.264 (RFC 6184) or H.265 (RFC 7798) access units from RTP packets.
//
// Latency policy: there is no jitter buffer. A frame is delivered the moment its marker packet
// arrives (or, if the sender never sets the marker, when the next timestamp begins). A packet
// that arrives after its successor is dropped and counted as late; the frame it belonged to is
// flagged corrupt instead of being held back.
//
// Parameter sets are removed from the access unit and exposed separately. Frames are held back
// only until parameter sets and a first keyframe exist, so the decoder never starts mid-GOP.
class Depacketizer {
  public:
    using FrameSink = std::function<void(const AssembledFrame&)>;

    // Give up waiting for a keyframe after this long; some encoders use intra refresh only.
    static constexpr int64_t kKeyframeWaitNs = 3'000'000'000;

    Depacketizer(imago_codec codec, imago_nal_format format);

    // Parameter sets announced out of band (SDP sprop-*). Raw NAL units.
    void setParameterSets(const ParameterSets& sets);

    void push(const RtpPacket& packet, int64_t nowNs, const FrameSink& sink);

    // Forgets the sequence and the frame in progress (new RTSP session).
    void reset();

    imago_codec codec() const { return codec_; }
    const ParameterSets& parameterSets() const { return params_; }
    const DepacketizerStats& stats() const { return stats_; }
    bool markerSeen() const { return markerSeen_; }

  private:
    // Whole NAL unit in one piece.
    void appendNal(const uint8_t* nal, size_t size);
    // NAL unit split across packets (FU-A / FU).
    void beginFragment(const uint8_t* header, size_t headerSize);
    void appendFragment(const uint8_t* data, size_t size);
    void endFragment();
    void abortFragment();

    // Classifies a complete NAL unit. Returns false if it must not go into the access unit.
    bool acceptNal(const uint8_t* nal, size_t size);
    void storeParameterSet(std::vector<uint8_t>& slot, const uint8_t* nal, size_t size);
    void writePrefix(size_t offset, size_t nalSize);

    void parseH264(const uint8_t* payload, size_t size);
    void parseH265(const uint8_t* payload, size_t size);

    void deliver(int64_t nowNs, const FrameSink& sink);
    void resetFrame();
    bool haveParameterSets() const;
    void markLoss();

    const imago_codec codec_;
    const imago_nal_format format_;

    std::vector<uint8_t> frame_;
    bool frameActive_ = false;
    uint32_t frameTimestamp_ = 0;
    bool frameCorrupt_ = false;
    bool frameKeyframe_ = false;
    int64_t frameFirstNs_ = 0;
    int64_t frameLastNs_ = 0;

    bool fragmentActive_ = false;
    size_t fragmentOffset_ = 0;

    bool sequenceKnown_ = false;
    uint16_t expectedSequence_ = 0;
    bool lossBeforeNextFrame_ = false;

    ParameterSets params_;
    bool parameterSetsChanged_ = false;
    bool waitingForKeyframe_ = true;
    int64_t waitStartNs_ = 0;
    bool markerSeen_ = false;
    uint32_t frameNumber_ = 0;

    bool transitKnown_ = false;
    double lastTransit_ = 0;
    double jitterTicks_ = 0;

    DepacketizerStats stats_;
};

} // namespace imago
