#pragma once

#include <imago/imago_core.h>

#include <QList>
#include <QString>
#include <QVariantList>

/// The aircraft's own figures, when the autopilot's telemetry reaches the app (Herelink
/// forwarding, or the profile's "Aircraft telemetry" listener): altitude above home, ground and
/// air speed, battery. As the Quest shows them in its bottom bar.
namespace Aircraft {

/// The battery turns amber under this percentage, red under the other.
constexpr int kBatteryWarnPercent = 30;
constexpr int kBatteryLowPercent = 15;

struct Figure {
    enum class Level { Normal, Warning, Danger };
    /// "ALT", "GS", "AS", "BAT".
    QString caption;
    /// "120 m", "14 m/s", "22.8 V  87 %".
    QString value;
    Level level = Level::Normal;
    bool operator==(const Figure&) const = default;
};

/// In display order; empty when nothing is valid (the pill is then hidden). Airspeed only from
/// 0.5 m/s: a vehicle without the sensor reports 0.
QList<Figure> figures(const imago_state& s);

/// For QML: [{caption, value, level}] with level 0 normal, 1 warning, 2 danger.
QVariantList toVariant(const QList<Figure>& figures);

} // namespace Aircraft
