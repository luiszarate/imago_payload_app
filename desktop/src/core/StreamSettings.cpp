#include "core/StreamSettings.h"

#include <imago/imago_core.h>

QList<int> StreamSettings::cameras() { return {IMAGO_STREAM_CAMERA_EO, IMAGO_STREAM_CAMERA_IR}; }

QString StreamSettings::cameraTitle(int camera) {
    switch (camera) {
    case IMAGO_STREAM_CAMERA_EO:
        return QStringLiteral("EO");
    case IMAGO_STREAM_CAMERA_IR:
        return QStringLiteral("IR");
    default:
        return {};
    }
}

bool StreamSettings::isCamera(int camera) { return !cameraTitle(camera).isEmpty(); }

QStringList StreamSettings::sizeTitles(int camera) {
    QStringList titles;
    if (!isCamera(camera)) {
        return titles;
    }
    const auto cam = static_cast<uint32_t>(camera);
    const uint32_t count = imago_stream_resolution_count(cam);
    for (uint32_t level = 0; level < count; ++level) {
        uint32_t width = 0;
        uint32_t height = 0;
        if (!imago_stream_resolution_size(cam, level, &width, &height)) {
            break;
        }
        titles.append(QStringLiteral("%1×%2").arg(width).arg(height));
    }
    return titles;
}

QStringList StreamSettings::rangeFinderRates() {
    return {QStringLiteral("1 Hz"), QStringLiteral("4 Hz"), QStringLiteral("10 Hz"), QStringLiteral("Off")};
}
