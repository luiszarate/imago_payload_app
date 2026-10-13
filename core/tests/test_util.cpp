#include "rtsp/rtsp_message.h"
#include "util/base64.h"
#include "util/md5.h"
#include "util/stats.h"
#include "util/text.h"

#include <doctest.h>

using namespace imago;

TEST_CASE("base64 round trip and known vectors") {
    CHECK(base64Encode("") == "");
    CHECK(base64Encode("f") == "Zg==");
    CHECK(base64Encode("fo") == "Zm8=");
    CHECK(base64Encode("foo") == "Zm9v");
    CHECK(base64Encode("foobar") == "Zm9vYmFy");
    const auto decoded = base64Decode("Zm9vYmE=");
    REQUIRE(decoded);
    CHECK(std::string(decoded->begin(), decoded->end()) == "fooba");
    CHECK(base64Decode("Zm9vYmE").has_value()); // padding is optional
    CHECK_FALSE(base64Decode("Zm9v*mE=").has_value());
}

TEST_CASE("md5 known vectors") {
    CHECK(Md5::hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(Md5::hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(Md5::hex("The quick brown fox jumps over the lazy dog") ==
          "9e107d9d372bb6826bd81d3542a419d6");
    // Crosses the 56-byte padding boundary.
    CHECK(Md5::hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

TEST_CASE("digest authorization matches RFC 2617 example") {
    RtspAuthChallenge c;
    c.digest = true;
    c.realm = "testrealm@host.com";
    c.nonce = "dcd98b7102dd2f0e8b11d0f600bfb0c093";
    c.opaque = "5ccc069c403ebaf9f0171e9517f40e41";
    c.qop = "auth";
    const std::string header = buildAuthorization(c, "GET", "/dir/index.html", "Mufasa",
                                                  "Circle Of Life", 1, "0a4f113b");
    CHECK(header.find("response=\"6629fae49393a05397450978507c4ef1\"") != std::string::npos);
    CHECK(header.find("nc=00000001") != std::string::npos);
}

TEST_CASE("text helpers") {
    CHECK(text::trim("  a b \r\n") == "a b");
    CHECK(text::iequals("Content-Length", "content-length"));
    CHECK(text::istartsWith("RTP/AVP/TCP;unicast", "rtp/avp/tcp"));
    CHECK(text::split("a;b;;c", ';').size() == 4);
    CHECK(text::parseInt(" 42 ") == 42);
    CHECK_FALSE(text::parseInt("4x2").has_value());
    char small[4];
    text::copyTo(small, "abcdef");
    CHECK(std::string(small) == "abc");
}

TEST_CASE("windowed percentile ignores old samples") {
    WindowedPercentile p;
    const int64_t s = 1'000'000'000;
    for (int i = 1; i <= 100; ++i) {
        p.add(s, static_cast<float>(i));
    }
    CHECK(p.percentile(s, s, 0.95f) == doctest::Approx(95).epsilon(0.02));
    CHECK(p.percentile(s * 5, s, 0.95f) == 0.0f);
}
