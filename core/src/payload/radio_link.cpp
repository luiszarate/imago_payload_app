#include "payload/radio_link.h"

#include "mavlink/mavlink.h"

namespace imago::radio {

namespace {
constexpr int64_t kNsPerMs = 1'000'000;
constexpr int64_t kLostNs = int64_t{IMAGO_RADIO_LOST_MS} * kNsPerMs;
constexpr int64_t kHoldNs = 3'000'000'000;
} // namespace

bool decodeDbm(uint8_t raw, int32_t& dbm) {
    if (raw == 0 || raw == UINT8_MAX) {
        return false;
    }
    // Over 127: a signed dBm byte (161 is -95). Under: the Herelink's magnitude (95 is -95).
    dbm = raw > 127 ? static_cast<int32_t>(raw) - 256 : -static_cast<int32_t>(raw);
    return true;
}

imago_radio_level classify(int32_t rssiDbm, int32_t snrDb) {
    if (snrDb < IMAGO_RADIO_CRITICAL_SNR_DB || rssiDbm < IMAGO_RADIO_CRITICAL_RSSI_DBM) {
        return IMAGO_RADIO_CRITICAL;
    }
    if (snrDb < IMAGO_RADIO_WEAK_SNR_DB) {
        return IMAGO_RADIO_WEAK;
    }
    return IMAGO_RADIO_GOOD;
}

void RadioLink::handle(uint8_t sysid, uint8_t compid, uint8_t rssi, uint8_t noise, int64_t nowNs) {
    if (compid != MAV_COMP_ID_TELEMETRY_RADIO || (system_ != 0 && sysid != system_)) {
        return;
    }
    int32_t rssiDbm = 0;
    int32_t noiseDbm = 0;
    if (!decodeDbm(rssi, rssiDbm) || !decodeDbm(noise, noiseDbm)) {
        return;
    }
    system_ = sysid;
    // After a silence the old level says nothing about the new readings.
    const bool resumed = lastNs_ == 0 || nowNs - lastNs_ > kLostNs;
    rssiDbm_ = rssiDbm;
    noiseDbm_ = noiseDbm;
    lastNs_ = nowNs;
    const imago_radio_level level = classify(rssiDbm, rssiDbm - noiseDbm);
    // Worse at once; better only once the worse level has lasted kHoldNs.
    if (resumed || level >= held_ || nowNs - heldSinceNs_ >= kHoldNs) {
        held_ = level;
        heldSinceNs_ = nowNs;
    }
}

void RadioLink::snapshot(int64_t nowNs, imago_state& out) const {
    out.radio_valid = lastNs_ != 0 && nowNs - lastNs_ <= kLostNs;
    out.radio_rssi_dbm = out.radio_valid ? rssiDbm_ : 0;
    out.radio_noise_dbm = out.radio_valid ? noiseDbm_ : 0;
    out.radio_snr_db = out.radio_valid ? rssiDbm_ - noiseDbm_ : 0;
    if (lastNs_ == 0) {
        out.radio_level = IMAGO_RADIO_UNKNOWN;
    } else {
        out.radio_level = out.radio_valid ? held_ : IMAGO_RADIO_LOST;
    }
}

} // namespace imago::radio
