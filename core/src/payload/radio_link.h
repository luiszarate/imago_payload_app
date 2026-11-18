#pragma once

#include "imago/imago_core.h"

#include <cstdint>

namespace imago::radio {

// RADIO_STATUS (#109) as the Herelink sends it to the ground: from 42/68 (its own system, the
// telemetry radio component) about every 0.6 s, rssi and noise as magnitudes (95 is -95 dBm),
// remrssi and remnoise always 0 (every .tlog up to 2026-11-10). Radios that send signed dBm
// (QGroundControl reads non-SiK radios that way) put values over 127 in the byte.
// 0 and 255 are no reading. Returns false for those.
bool decodeDbm(uint8_t raw, int32_t& dbm);

// The level one reading deserves: SNR under IMAGO_RADIO_WEAK_SNR_DB is weak; under
// IMAGO_RADIO_CRITICAL_SNR_DB, or RSSI under IMAGO_RADIO_CRITICAL_RSSI_DBM, critical.
imago_radio_level classify(int32_t rssiDbm, int32_t snrDb);

// Follows one radio's RADIO_STATUS and works out what the apps show. No clock of its own:
// every call takes the time. Not thread-safe (PayloadModel holds it under its lock).
class RadioLink {
  public:
    // A RADIO_STATUS from sysid/compid. Only the telemetry radio component counts, and of the
    // first system heard (a second radio on the link would make the figures jump).
    void handle(uint8_t sysid, uint8_t compid, uint8_t rssi, uint8_t noise, int64_t nowNs);
    // Fills imago_state.radio_*.
    void snapshot(int64_t nowNs, imago_state& out) const;

  private:
    uint8_t system_ = 0;
    int64_t lastNs_ = 0;
    int32_t rssiDbm_ = 0;
    int32_t noiseDbm_ = 0;
    // The worst level of the last kHoldNs: the readings swing a few dB every 0.6 s, and one
    // better reading must not clear a warning that the next one raises again.
    imago_radio_level held_ = IMAGO_RADIO_UNKNOWN;
    int64_t heldSinceNs_ = 0;
};

} // namespace imago::radio
