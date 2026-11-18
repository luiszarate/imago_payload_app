#include "core/Aircraft.h"

#include <QVariantMap>

#include <cmath>

namespace {

QString whole(float value, const char* unit) {
    return QStringLiteral("%1 %2").arg(std::lround(value)).arg(QLatin1String(unit));
}

} // namespace

QList<Aircraft::Figure> Aircraft::figures(const imago_state& s) {
    QList<Figure> result;
    if (s.vehicle_altitude_valid) {
        result.append({QStringLiteral("ALT"), whole(s.vehicle_altitude_m, "m")});
    }
    if (s.vehicle_speed_valid) {
        result.append({QStringLiteral("GS"), whole(s.vehicle_groundspeed_mps, "m/s")});
        if (s.vehicle_airspeed_mps > 0.5f) {
            result.append({QStringLiteral("AS"), whole(s.vehicle_airspeed_mps, "m/s")});
        }
    }
    if (s.vehicle_battery_valid) {
        const int percent = s.vehicle_battery_percent;
        QString value = QStringLiteral("%1 V").arg(static_cast<double>(s.vehicle_battery_v), 0, 'f', 1);
        if (percent >= 0) {
            value += QStringLiteral("  %1 %").arg(percent);
        }
        const Figure::Level level = percent < 0                     ? Figure::Level::Normal
                                    : percent < kBatteryLowPercent  ? Figure::Level::Danger
                                    : percent < kBatteryWarnPercent ? Figure::Level::Warning
                                                                    : Figure::Level::Normal;
        result.append({QStringLiteral("BAT"), value, level});
    }
    return result;
}

QVariantList Aircraft::toVariant(const QList<Figure>& figures) {
    QVariantList list;
    for (const Figure& figure : figures) {
        list.append(QVariantMap{{QStringLiteral("caption"), figure.caption},
                                {QStringLiteral("value"), figure.value},
                                {QStringLiteral("level"), static_cast<int>(figure.level)}});
    }
    return list;
}
