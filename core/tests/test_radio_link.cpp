// The Herelink's RADIO_STATUS: how its bytes read as dBm and the level the apps show. The
// model taking it from either link is in test_orus.cpp, the whole core against orus-sim's
// radio in test_session.cpp.

#include "payload/radio_link.h"

#include "mavlink/mavlink.h"

#include <doctest.h>

using namespace imago;
using namespace imago::radio;

namespace {

constexpr int64_t kMs = 1'000'000;
constexpr uint8_t kHerelink = 42;
constexpr uint8_t kRadio = MAV_COMP_ID_TELEMETRY_RADIO;

imago_state snapshotAt(const RadioLink& radio, int64_t nowNs) {
    imago_state st{};
    radio.snapshot(nowNs, st);
    return st;
}

} // namespace

TEST_CASE("radio: RADIO_STATUS bytes as dBm") {
    int32_t dbm = 0;
    // The Herelink's magnitudes: every .tlog up to 2026-11-10 had rssi 56-113, noise 83-122.
    REQUIRE(decodeDbm(95, dbm));
    CHECK(dbm == -95);
    REQUIRE(decodeDbm(56, dbm));
    CHECK(dbm == -56);
    // A signed byte, as other radios send it: 161 is -95.
    REQUIRE(decodeDbm(161, dbm));
    CHECK(dbm == -95);
    REQUIRE(decodeDbm(128, dbm));
    CHECK(dbm == -128);
    // No reading.
    CHECK_FALSE(decodeDbm(0, dbm));
    CHECK_FALSE(decodeDbm(255, dbm));
}

TEST_CASE("radio: the level from SNR and RSSI") {
    CHECK(classify(-70, 28) == IMAGO_RADIO_GOOD);
    CHECK(classify(-90, 6) == IMAGO_RADIO_GOOD);
    CHECK(classify(-90, 5) == IMAGO_RADIO_WEAK);
    CHECK(classify(-90, 3) == IMAGO_RADIO_WEAK);
    CHECK(classify(-90, 2) == IMAGO_RADIO_CRITICAL);
    CHECK(classify(-95, -4) == IMAGO_RADIO_CRITICAL); // the flight of 2026-10-15
    // Under the Herelink's sensitivity, however good the SNR.
    CHECK(classify(-100, 20) == IMAGO_RADIO_CRITICAL);
    CHECK(classify(-99, 20) == IMAGO_RADIO_GOOD);
}

TEST_CASE("radio: one radio followed, its level held at its worst, lost after 5 s") {
    RadioLink radio;
    const int64_t t0 = 10'000 * kMs;
    imago_state st = snapshotAt(radio, t0);
    CHECK_FALSE(st.radio_valid);
    CHECK(st.radio_level == IMAGO_RADIO_UNKNOWN);

    radio.handle(kHerelink, kRadio, 72, 100, t0);
    st = snapshotAt(radio, t0);
    CHECK(st.radio_valid);
    CHECK(st.radio_rssi_dbm == -72);
    CHECK(st.radio_noise_dbm == -100);
    CHECK(st.radio_snr_db == 28);
    CHECK(st.radio_level == IMAGO_RADIO_GOOD);

    // Only the telemetry radio component, of the first system heard.
    radio.handle(kHerelink, MAV_COMP_ID_AUTOPILOT1, 110, 112, t0);
    radio.handle(51, kRadio, 110, 112, t0);
    CHECK(snapshotAt(radio, t0).radio_rssi_dbm == -72);
    // A reading of nothing changes nothing.
    radio.handle(kHerelink, kRadio, 0, 100, t0);
    CHECK(snapshotAt(radio, t0).radio_rssi_dbm == -72);

    // Worse at once.
    radio.handle(kHerelink, kRadio, 98, 102, t0 + 600 * kMs); // SNR 4
    CHECK(snapshotAt(radio, t0 + 600 * kMs).radio_level == IMAGO_RADIO_WEAK);
    radio.handle(kHerelink, kRadio, 100, 102, t0 + 1200 * kMs); // SNR 2
    st = snapshotAt(radio, t0 + 1200 * kMs);
    CHECK(st.radio_level == IMAGO_RADIO_CRITICAL);
    CHECK(st.radio_snr_db == 2);
    // Better: the figures follow at once, the level only after 3 s.
    radio.handle(kHerelink, kRadio, 80, 105, t0 + 1800 * kMs);
    st = snapshotAt(radio, t0 + 1800 * kMs);
    CHECK(st.radio_snr_db == 25);
    CHECK(st.radio_level == IMAGO_RADIO_CRITICAL);
    radio.handle(kHerelink, kRadio, 80, 105, t0 + 3600 * kMs);
    CHECK(snapshotAt(radio, t0 + 3600 * kMs).radio_level == IMAGO_RADIO_CRITICAL);
    radio.handle(kHerelink, kRadio, 80, 105, t0 + 4200 * kMs);
    CHECK(snapshotAt(radio, t0 + 4200 * kMs).radio_level == IMAGO_RADIO_GOOD);

    // A pause of 3-4 s happens in every flight: still valid. Over 5 s: lost, no figures.
    const int64_t last = t0 + 4200 * kMs;
    CHECK(snapshotAt(radio, last + 4000 * kMs).radio_level == IMAGO_RADIO_GOOD);
    st = snapshotAt(radio, last + 5100 * kMs);
    CHECK_FALSE(st.radio_valid);
    CHECK(st.radio_level == IMAGO_RADIO_LOST);
    CHECK(st.radio_rssi_dbm == 0);

    // Back after a loss: the new readings alone, nothing held from before.
    radio.handle(kHerelink, kRadio, 100, 102, last + 6000 * kMs);
    radio.handle(kHerelink, kRadio, 70, 100, last + 20'000 * kMs);
    CHECK(snapshotAt(radio, last + 20'000 * kMs).radio_level == IMAGO_RADIO_GOOD);
}
