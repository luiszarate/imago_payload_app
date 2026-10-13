#include "rtsp/rtsp_message.h"
#include "rtsp/rtsp_url.h"
#include "rtsp/sdp.h"

#include <doctest.h>

#include <string>

using namespace imago;

TEST_CASE("RTSP URL parsing") {
    auto u = RtspUrl::parse("rtsp://192.168.43.1:8554/fpv_stream");
    REQUIRE(u);
    CHECK(u->host == "192.168.43.1");
    CHECK(u->port == 8554);
    CHECK(u->path == "/fpv_stream");
    CHECK(u->toString() == "rtsp://192.168.43.1:8554/fpv_stream");

    u = RtspUrl::parse("rtsp://admin:se:cret@cam.local/live?x=1");
    REQUIRE(u);
    CHECK(u->user == "admin");
    CHECK(u->password == "se:cret");
    CHECK(u->port == 554);
    CHECK(u->path == "/live?x=1");
    CHECK(u->toString() == "rtsp://cam.local/live?x=1"); // no credentials on the wire

    CHECK(RtspUrl::parse("rtsp://10.0.0.1")->path == "/");
    CHECK(u->withHost("192.168.55.1").toString() == "rtsp://192.168.55.1/live?x=1");
    CHECK_FALSE(RtspUrl::parse("http://x/").has_value());
    CHECK_FALSE(RtspUrl::parse("rtsp://host:99999/").has_value());
}

TEST_CASE("control URL resolution") {
    CHECK(resolveControlUrl("rtsp://h/s", "trackID=0") == "rtsp://h/s/trackID=0");
    CHECK(resolveControlUrl("rtsp://h/s/", "trackID=0") == "rtsp://h/s/trackID=0");
    CHECK(resolveControlUrl("rtsp://h/s", "*") == "rtsp://h/s");
    CHECK(resolveControlUrl("rtsp://h/s", "rtsp://other/x") == "rtsp://other/x");
}

TEST_CASE("SDP: H.264 with sprop-parameter-sets") {
    const char* sdp = "v=0\r\n"
                      "o=- 0 0 IN IP4 127.0.0.1\r\n"
                      "s=Stream\r\n"
                      "t=0 0\r\n"
                      "a=control:*\r\n"
                      "m=audio 0 RTP/AVP 97\r\n"
                      "a=rtpmap:97 MPEG4-GENERIC/48000/2\r\n"
                      "m=video 0 RTP/AVP 96\r\n"
                      "a=rtpmap:96 H264/90000\r\n"
                      "a=fmtp:96 packetization-mode=1;profile-level-id=42c01f;"
                      "sprop-parameter-sets=Z0LAH6o=,aM48gA==\r\n"
                      "a=control:trackID=1\r\n";
    const auto v = parseSdpVideo(sdp);
    REQUIRE(v);
    CHECK(v->codec == IMAGO_CODEC_H264);
    CHECK(v->payloadType == 96);
    CHECK(v->control == "trackID=1");
    CHECK(v->parameterSets.sps == std::vector<uint8_t>{0x67, 0x42, 0xC0, 0x1F, 0xAA});
    CHECK(v->parameterSets.pps == std::vector<uint8_t>{0x68, 0xCE, 0x3C, 0x80});
}

TEST_CASE("SDP: H.265 with sprop-vps/sps/pps") {
    const char* sdp = "v=0\n"
                      "m=video 0 RTP/AVP 97\n"
                      "a=rtpmap:97 H265/90000\n"
                      "a=fmtp:97 sprop-vps=QAEMAQ==; sprop-sps=QgEBAg==; sprop-pps=RAHBcg==\n";
    const auto v = parseSdpVideo(sdp);
    REQUIRE(v);
    CHECK(v->codec == IMAGO_CODEC_H265);
    CHECK(v->payloadType == 97);
    CHECK(v->parameterSets.vps.size() == 4);
    CHECK(v->parameterSets.sps.size() == 4);
    CHECK(v->parameterSets.pps.size() == 4);
}

TEST_CASE("SDP without supported video") {
    CHECK_FALSE(parseSdpVideo("v=0\nm=video 0 RTP/AVP 26\na=rtpmap:26 JPEG/90000\n").has_value());
}

TEST_CASE("stream parser splits responses and interleaved frames, across partial reads") {
    const std::string response = "RTSP/1.0 200 OK\r\nCSeq: 3\r\nSession: ABC123;timeout=30\r\n"
                                 "Content-Length: 4\r\n\r\nbody";
    std::vector<uint8_t> stream = {'$', 0, 0, 3, 0xAA, 0xBB, 0xCC};
    stream.insert(stream.end(), response.begin(), response.end());
    stream.insert(stream.end(), {'$', 1, 0, 1, 0x11});

    RtspStreamParser parser;
    RtspResponse r;
    uint8_t channel = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;

    // Feed in 5-byte pieces.
    std::vector<RtspStreamParser::Item> items;
    for (size_t i = 0; i < stream.size(); i += 5) {
        parser.append(stream.data() + i, std::min<size_t>(5, stream.size() - i));
        while (true) {
            const auto item = parser.next(r, channel, data, size);
            if (item == RtspStreamParser::Item::None) {
                break;
            }
            items.push_back(item);
            if (item == RtspStreamParser::Item::Interleaved && channel == 0) {
                CHECK(size == 3);
                CHECK(data[0] == 0xAA);
            }
            if (item == RtspStreamParser::Item::Response) {
                CHECK(r.status == 200);
                CHECK(r.cseq() == 3);
                CHECK(r.body == "body");
                CHECK(r.header("session") == "ABC123;timeout=30");
            }
        }
    }
    REQUIRE(items.size() == 3);
    CHECK(items[0] == RtspStreamParser::Item::Interleaved);
    CHECK(items[1] == RtspStreamParser::Item::Response);
    CHECK(items[2] == RtspStreamParser::Item::Interleaved);
}

TEST_CASE("Session and Transport headers") {
    const auto s = parseSessionHeader("12345678;timeout=30");
    CHECK(s.id == "12345678");
    CHECK(s.timeoutSec == 30);
    CHECK(parseSessionHeader("abc").timeoutSec == 60);

    auto t = parseTransportHeader("RTP/AVP;unicast;client_port=50000-50001;server_port=6970-6971;ssrc=1A2B3C4D");
    CHECK_FALSE(t.tcp);
    CHECK(t.serverRtpPort == 6970);
    CHECK(t.serverRtcpPort == 6971);
    REQUIRE(t.ssrc);
    CHECK(*t.ssrc == 0x1A2B3C4D);

    t = parseTransportHeader("RTP/AVP/TCP;unicast;interleaved=2-3");
    CHECK(t.tcp);
    CHECK(t.interleavedRtp == 2);
    CHECK(t.interleavedRtcp == 3);
}

TEST_CASE("WWW-Authenticate parsing") {
    auto c = parseAuthChallenge("Digest realm=\"IP Camera\", nonce=\"a1b2, c3\", qop=\"auth,auth-int\"");
    REQUIRE(c);
    CHECK(c->digest);
    CHECK(c->realm == "IP Camera");
    CHECK(c->nonce == "a1b2, c3");
    CHECK(c->qop == "auth");
    c = parseAuthChallenge("Basic realm=\"x\"");
    REQUIRE(c);
    CHECK_FALSE(c->digest);
    CHECK(buildAuthorization(*c, "DESCRIBE", "rtsp://x/", "user", "pass", 1, "") ==
          "Basic dXNlcjpwYXNz");
}
