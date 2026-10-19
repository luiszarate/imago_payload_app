#include "core/PayloadMessages.h"

#include <imago/imago_core.h>

QString PayloadMessages::paramName(const QString& id) {
    if (id == QLatin1String("GB_MODE")) return QStringLiteral("Gimbal mode");
    if (id == QLatin1String("C_SOURCE")) return QStringLiteral("View");
    if (id == QLatin1String("TRACK_MODE")) return QStringLiteral("AI detection");
    if (id == QLatin1String("RC_MODE")) return QStringLiteral("RC mode");
    if (id == QLatin1String("C_T_G")) return QStringLiteral("IR gain");
    if (id.startsWith(QLatin1String("C_T_ISO")) || id.startsWith(QLatin1String("C_T_RG"))) {
        return QStringLiteral("Isotherm");
    }
    return id;
}

QString PayloadMessages::trackingIgnored(const QString& command, int trackingState) {
    // Field test 2026-10-15: accepted for minutes, never applied, until the view changed.
    const QString still = trackingState == IMAGO_TRACKING_TRACKED ? QStringLiteral("still tracking")
                          : trackingState == IMAGO_TRACKING_LOST  ? QStringLiteral("target lost")
                                                                  : QStringLiteral("idle");
    if (command == QLatin1String("stop")) {
        return QStringLiteral("Payload accepted Stop but is %1. Try changing the view, or restart the camera service in Settings.").arg(still);
    }
    return QStringLiteral("Payload accepted the point but did not track it (%1). Try changing the view, or restart the camera service in Settings.").arg(still);
}

QString PayloadMessages::webFailed(WebRequest request, uint32_t status, const QString& error) {
    const QString what = request == WebRequest::Read   ? QStringLiteral("Reading")
                         : request == WebRequest::Save ? QStringLiteral("Saving")
                                                       : QStringLiteral("Restarting");
    return QStringLiteral("%1 the payload's web settings failed: %2")
        .arg(what, error.isEmpty() ? QStringLiteral("code %1").arg(status) : error);
}

std::optional<QString> PanLimitWarning::update(bool nearLimit, bool hasPan, float pan) {
    if (nearLimit == warned_) {
        return std::nullopt;
    }
    warned_ = nearLimit;
    if (!warned_ || !hasPan) {
        return std::nullopt;
    }
    return QString::asprintf("Gimbal pan %+.0f°: near its ±%.0f° limit", static_cast<double>(pan),
                             static_cast<double>(IMAGO_GIMBAL_PAN_LIMIT_DEG));
}
