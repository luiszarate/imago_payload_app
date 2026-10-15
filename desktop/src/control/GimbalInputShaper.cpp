#include "control/GimbalInputShaper.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace {

const char* kKey = "controls.gimbalShaper";

/// A digital input: -1, 0 or 1 (a half-pressed analog button counts once past half way).
float sign(float value) {
    if (std::fabs(value) < 0.5f) {
        return 0.0f;
    }
    return value < 0 ? -1.0f : 1.0f;
}

} // namespace

GimbalInputShaper::Rates GimbalInputShaper::rates(float x, float y, float zoom, bool digitalX,
                                                  bool digitalY) const {
    const float zoomFactor = 1.0f / std::sqrt(std::max(1.0f, zoom));
    const float yaw = digitalX ? sign(x) * digitalYawDps : shape(x) * maxYawDps;
    const float pitch = digitalY ? sign(y) * digitalPitchDps : shape(y) * maxPitchDps;
    return {pitch * zoomFactor, yaw * zoomFactor};
}

float GimbalInputShaper::shape(float value) const {
    const float magnitude = std::min(std::fabs(value), 1.0f);
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
    const float curved = (1.0f - expo) * normalized + expo * normalized * normalized * normalized;
    return value < 0 ? -curved : curved;
}

QVariantMap GimbalInputShaper::toVariant() const {
    return {
        {QStringLiteral("maxYawDps"), maxYawDps},
        {QStringLiteral("maxPitchDps"), maxPitchDps},
        {QStringLiteral("deadzone"), deadzone},
        {QStringLiteral("expo"), expo},
        {QStringLiteral("digitalYawDps"), digitalYawDps},
        {QStringLiteral("digitalPitchDps"), digitalPitchDps},
    };
}

bool GimbalInputShaper::set(const QString& field, double value) {
    const auto v = static_cast<float>(value);
    const float speed = std::clamp(v, kMinSpeed, kMaxSpeed);
    if (field == QLatin1String("maxYawDps")) {
        maxYawDps = speed;
    } else if (field == QLatin1String("maxPitchDps")) {
        maxPitchDps = speed;
    } else if (field == QLatin1String("digitalYawDps")) {
        digitalYawDps = speed;
    } else if (field == QLatin1String("digitalPitchDps")) {
        digitalPitchDps = speed;
    } else if (field == QLatin1String("deadzone")) {
        deadzone = std::clamp(v, 0.0f, kMaxDeadzone);
    } else if (field == QLatin1String("expo")) {
        expo = std::clamp(v, 0.0f, 1.0f);
    } else {
        return false;
    }
    return true;
}

void GimbalInputShaper::save(QSettings& settings) const {
    settings.setValue(QLatin1String(kKey),
                      QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(toVariant()))
                                            .toJson(QJsonDocument::Compact)));
}

GimbalInputShaper GimbalInputShaper::load(const QSettings& settings) {
    GimbalInputShaper shaper;
    const QJsonObject json =
        QJsonDocument::fromJson(settings.value(QLatin1String(kKey)).toByteArray()).object();
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.value().isDouble()) {
            shaper.set(it.key(), it.value().toDouble());
        }
    }
    return shaper;
}
