#pragma once

#include <QString>

#include <cstdint>
#include <optional>

/// A request to the payload's web settings page (imago_web_*). AutoRead: the thermal bar's own
/// read of the isotherm, quiet but for one notice per connection.
enum class WebRequest { Read, AutoRead, Save, RestartService };

/// Toast texts about what the payload did, as in the iOS app.
namespace PayloadMessages {

/// A setting's name in a "… was rejected" toast: "View", "Isotherm"... else the param id.
QString paramName(const QString& id);

/// The payload accepted a tracking command ("track" or "stop") and then did not follow it;
/// trackingState is the imago_tracking_state it reported.
QString trackingIgnored(const QString& command, int trackingState);

/// A web settings request that failed: status is the HTTP status (0: no answer), error what
/// the core says failed.
QString webFailed(WebRequest request, uint32_t status, const QString& error);

} // namespace PayloadMessages

/// Warns once each time the gimbal pan gets near its ±320° stop (the core's near-limit flag,
/// with hysteresis): the yaw readouts stay amber meanwhile. Reset on disconnect.
class PanLimitWarning {
  public:
    /// The toast to show when this state starts an approach to the stop.
    std::optional<QString> update(bool nearLimit, bool hasPan, float pan);
    void reset() { warned_ = false; }

  private:
    bool warned_ = false;
};

/// Warns once when the radio link turns critical or is lost (the core's imago_radio_level); the
/// chip in the top bar keeps showing it, and weak is only the chip's colour. Lost after critical
/// is the same episode; it warns again only after the link was better than critical. Reset on
/// disconnect.
class RadioWarning {
  public:
    /// The notice to show for this reading; rssi and snr count only while valid.
    std::optional<QString> update(int level, bool valid, int rssi, int snr);
    void reset() { warned_ = 0; }

  private:
    /// The level seen last (imago_radio_level).
    int warned_ = 0;
};
