#pragma once

#include <QString>

#include <cstdint>
#include <optional>

/// A request to the payload's web settings page (imago_web_*).
enum class WebRequest { Read, Save, RestartService };

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
