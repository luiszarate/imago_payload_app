#include "rtp/depacketizer.h"
#include "rtp/rtp_packet.h"
#include "test_helpers.h"

#include <doctest.h>

#include <string>
#include <vector>

using namespace imago;
using namespace imago::test;

namespace {

const std::vector<uint8_t> kSps = bytes({0x67, 0x42, 0xC0, 0x1F, 0xAA});
const std::vector<uint8_t> kPps = bytes({0x68, 0xCE, 0x3C, 0x80});
const std::vector<uint8_t> kIdr = bytes({0x65, 0x88, 0x84, 0x00, 0x11, 0x22});
const std::vector<uint8_t> kSlice = bytes({0x41, 0x9A, 0x02, 0x03});

struct Collector {
    struct Frame {
        std::vector<uint8_t> data;
        bool keyframe;
        bool corrupt;
        bool paramsChanged;
        uint32_t ts;
    };
    std::vector<Frame> frames;
    Depacketizer::FrameSink sink() {
        return [this](const AssembledFrame& f) {
            frames.push_back({std::vector<uint8_t>(f.data, f.data + f.size), f.keyframe, f.corrupt,
                              f.parameterSetsChanged, f.rtpTimestamp});
        };
    }
};

void push(Depacketizer& d, Collector& c, const std::vector<uint8_t>& packet, int64_t now = 1) {
    const auto parsed = parseRtp(packet.data(), packet.size());
    REQUIRE(parsed);
    d.push(*parsed, now, c.sink());
}

// FU-A fragments of one NAL unit, split into chunks of `chunk` payload bytes.
std::vector<std::vector<uint8_t>> fuA(const std::vector<uint8_t>& nal, size_t chunk) {
    std::vector<std::vector<uint8_t>> out;
    const uint8_t indicator = static_cast<uint8_t>((nal[0] & 0xE0) | 28);
    const uint8_t type = nal[0] & 0x1F;
    for (size_t offset = 1; offset < nal.size(); offset += chunk) {
        const size_t n = std::min(chunk, nal.size() - offset);
        uint8_t header = type;
        if (offset == 1) header |= 0x80;
        if (offset + n == nal.size()) header |= 0x40;
        std::vector<uint8_t> payload = {indicator, header};
        payload.insert(payload.end(), nal.begin() + static_cast<long>(offset),
                       nal.begin() + static_cast<long>(offset + n));
        out.push_back(payload);
    }
    return out;
}

} // namespace

TEST_CASE("parseRtp handles CSRC, extension and padding") {
    std::vector<uint8_t> p = {0xB1, 0xE0, 0x00, 0x05, 0, 0, 0x10, 0, 1, 2, 3, 4};
    // one CSRC
    p.insert(p.end(), {9, 9, 9, 9});
    // extension: profile 0xBEDE, length 1 word
    p.insert(p.end(), {0xBE, 0xDE, 0x00, 0x01, 1, 2, 3, 4});
    // payload + 2 bytes padding
    p.insert(p.end(), {0xAA, 0xBB, 0x00, 0x02});
    const auto r = parseRtp(p.data(), p.size());
    REQUIRE(r);
    CHECK(r->marker);
    CHECK(r->payloadType == 96);
    CHECK(r->sequence == 5);
    CHECK(r->timestamp == 0x1000);
    REQUIRE(r->payloadSize == 2);
    CHECK(r->payload[0] == 0xAA);
    CHECK_FALSE(parseRtp(p.data(), 8).has_value());
}

TEST_CASE("H.264: parameter sets, STAP-A and FU-A build an AVCC keyframe") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    Collector c;
    // STAP-A with SPS + PPS
    std::vector<uint8_t> stap = {24};
    stap.push_back(0);
    stap.push_back(static_cast<uint8_t>(kSps.size()));
    stap.insert(stap.end(), kSps.begin(), kSps.end());
    stap.push_back(0);
    stap.push_back(static_cast<uint8_t>(kPps.size()));
    stap.insert(stap.end(), kPps.begin(), kPps.end());
    push(d, c, rtp(10, 9000, false, stap));

    const auto fragments = fuA(kIdr, 2);
    uint16_t seq = 11;
    for (size_t i = 0; i < fragments.size(); ++i) {
        push(d, c, rtp(seq++, 9000, i + 1 == fragments.size(), fragments[i]));
    }
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].keyframe);
    CHECK_FALSE(c.frames[0].corrupt);
    CHECK(c.frames[0].paramsChanged);
    CHECK(c.frames[0].data == avcc(kIdr)); // SPS/PPS removed from the access unit
    CHECK(d.parameterSets().sps == kSps);
    CHECK(d.parameterSets().pps == kPps);

    // Next frame: no parameter set change.
    push(d, c, rtp(seq++, 12000, true, kSlice));
    REQUIRE(c.frames.size() == 2);
    CHECK_FALSE(c.frames[1].keyframe);
    CHECK_FALSE(c.frames[1].paramsChanged);
    CHECK(c.frames[1].data == avcc(kSlice));
    CHECK(d.stats().packetsLost == 0);
}

TEST_CASE("H.264: Annex B output uses start codes") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_ANNEXB);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(1, 100, true, kIdr));
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].data == concat(bytes({0, 0, 0, 1}), kIdr));
    CHECK(c.frames[0].paramsChanged); // first frame after SDP parameter sets
}

TEST_CASE("H.264: frames wait for parameter sets and the first keyframe") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    Collector c;
    push(d, c, rtp(1, 100, true, kSlice)); // no SPS/PPS yet
    CHECK(c.frames.empty());
    d.setParameterSets({{}, kSps, kPps});
    push(d, c, rtp(2, 200, true, kSlice)); // not a keyframe
    CHECK(c.frames.empty());
    push(d, c, rtp(3, 300, true, kIdr));
    REQUIRE(c.frames.size() == 1);
    push(d, c, rtp(4, 400, true, kSlice));
    CHECK(c.frames.size() == 2);
    CHECK(d.stats().framesDropped == 2);
}

TEST_CASE("H.264: keyframe wait gives up after the timeout (intra refresh streams)") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(1, 100, true, kSlice), 1);
    CHECK(c.frames.empty());
    push(d, c, rtp(2, 200, true, kSlice), 1 + Depacketizer::kKeyframeWaitNs + 1);
    CHECK(c.frames.size() == 1);
}

TEST_CASE("H.264: a lost fragment marks the frame corrupt without stalling") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(1, 100, true, kIdr));
    REQUIRE(c.frames.size() == 1);

    // Frame at ts 200: one complete slice, then a fragmented slice missing its 2nd fragment.
    push(d, c, rtp(2, 200, false, kSlice));
    std::vector<uint8_t> bigSlice = kSlice;
    bigSlice.insert(bigSlice.end(), 20, 0x55);
    const auto fragments = fuA(bigSlice, 4);
    REQUIRE(fragments.size() >= 4);
    uint16_t seq = 3;
    for (size_t i = 0; i < fragments.size(); ++i) {
        if (i != 1) {
            push(d, c, rtp(seq, 200, i + 1 == fragments.size(), fragments[i]));
        }
        ++seq;
    }
    REQUIRE(c.frames.size() == 2);
    CHECK(c.frames[1].corrupt);
    CHECK(c.frames[1].data == avcc(kSlice)); // the damaged NAL unit is left out
    CHECK(d.stats().packetsLost == 1);

    // The next frame is clean again.
    push(d, c, rtp(seq++, 300, true, kSlice));
    REQUIRE(c.frames.size() == 3);
    CHECK_FALSE(c.frames[2].corrupt);
}

TEST_CASE("H.264: a frame lost entirely flags the following frame") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(1, 100, true, kIdr));
    const auto fragments = fuA(concat(kSlice, std::vector<uint8_t>(12, 0x33)), 4);
    // Only the first fragment arrives, then the marker fragment after a gap.
    push(d, c, rtp(2, 200, false, fragments[0]));
    push(d, c, rtp(static_cast<uint16_t>(1 + fragments.size()), 200, true, fragments.back()));
    CHECK(c.frames.size() == 1); // nothing usable in frame 200
    push(d, c, rtp(static_cast<uint16_t>(2 + fragments.size()), 300, true, kSlice));
    REQUIRE(c.frames.size() == 2);
    CHECK(c.frames[1].corrupt); // it references the lost frame
}

TEST_CASE("H.264: late and duplicate packets are dropped") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(10, 100, true, kIdr));
    push(d, c, rtp(10, 100, true, kIdr)); // duplicate
    push(d, c, rtp(9, 50, true, kSlice));  // late
    CHECK(c.frames.size() == 1);
    CHECK(d.stats().packetsLate == 2);
}

TEST_CASE("H.264: frames are delivered on timestamp change when the marker is missing") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(1, 100, false, kIdr));
    CHECK(c.frames.empty());
    push(d, c, rtp(2, 200, false, kSlice));
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].ts == 100);
    CHECK_FALSE(d.markerSeen());
}

TEST_CASE("H.264: sequence wrap-around is not counted as loss") {
    Depacketizer d(IMAGO_CODEC_H264, IMAGO_NAL_AVCC);
    d.setParameterSets({{}, kSps, kPps});
    Collector c;
    push(d, c, rtp(65535, 100, true, kIdr));
    push(d, c, rtp(0, 200, true, kSlice));
    CHECK(c.frames.size() == 2);
    CHECK(d.stats().packetsLost == 0);
}

TEST_CASE("H.265: AP with VPS/SPS/PPS and FU-fragmented IDR") {
    const std::vector<uint8_t> vps = bytes({0x40, 0x01, 0x0C, 0x01});
    const std::vector<uint8_t> sps = bytes({0x42, 0x01, 0x01, 0x02});
    const std::vector<uint8_t> pps = bytes({0x44, 0x01, 0xC1, 0x72});
    // IDR_W_RADL (19): header (19 << 1) = 0x26, 0x01
    const std::vector<uint8_t> idr = bytes({0x26, 0x01, 0xAF, 0x01, 0x02, 0x03, 0x04, 0x05});

    Depacketizer d(IMAGO_CODEC_H265, IMAGO_NAL_AVCC);
    Collector c;
    std::vector<uint8_t> ap = {48 << 1, 0x01};
    for (const auto* nal : {&vps, &sps, &pps}) {
        ap.push_back(0);
        ap.push_back(static_cast<uint8_t>(nal->size()));
        ap.insert(ap.end(), nal->begin(), nal->end());
    }
    push(d, c, rtp(1, 100, false, ap));

    // FU: PayloadHdr (type 49), FU header (S/E + type 19)
    const std::vector<uint8_t> fu1 = concat(bytes({49 << 1, 0x01, 0x80 | 19}), bytes({0xAF, 0x01, 0x02}));
    const std::vector<uint8_t> fu2 = concat(bytes({49 << 1, 0x01, 0x40 | 19}), bytes({0x03, 0x04, 0x05}));
    push(d, c, rtp(2, 100, false, fu1));
    push(d, c, rtp(3, 100, true, fu2));

    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].keyframe);
    CHECK(c.frames[0].data == avcc(idr));
    CHECK(d.parameterSets().vps == vps);
    CHECK(d.parameterSets().sps == sps);
    CHECK(d.parameterSets().pps == pps);
}
