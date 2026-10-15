#include "core/Thermal.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

const char* kLowKey = "thermal.isothermLow";
const char* kHighKey = "thermal.isothermHigh";

/// A camera value, -1 if the camera has not reported it.
int value(const QVariantMap& values, const char* id) {
    const auto it = values.constFind(QLatin1String(id));
    return it == values.cend() ? -1 : it->toInt();
}

} // namespace

QList<Thermal::Preset> Thermal::presets() {
    return {
        {QStringLiteral("People"), {35, 40}},
        {QStringLiteral("Hot"), {100, 150}},
        {QStringLiteral("Fire"), {150, 550}},
    };
}

bool Thermal::isValid(const Band& band) { return band.low >= kMinC && band.low < band.high && band.high <= kMaxC; }

int Thermal::clampLow(int value, int high) { return std::clamp(value, kMinC, std::max(kMinC, high - 1)); }

int Thermal::clampHigh(int value, int low) { return std::clamp(value, std::min(kMaxC, low + 1), kMaxC); }

Thermal::Band Thermal::rememberedBand(const QSettings& settings) {
    const Band band{settings.value(QLatin1String(kLowKey), 35).toInt(),
                    settings.value(QLatin1String(kHighKey), 40).toInt()};
    return isValid(band) ? band : Band{};
}

void Thermal::rememberBand(QSettings& settings, const Band& band) {
    settings.setValue(QLatin1String(kLowKey), band.low);
    settings.setValue(QLatin1String(kHighKey), band.high);
}

QString Thermal::paletteName(int cameraType, int index) {
    if (index < 0 || index >= IMAGO_IR_PALETTE_COUNT) {
        return QStringLiteral("Palette %1").arg(index + 1);
    }
    return QString::fromUtf8(imago_ir_palette_name(cameraType, static_cast<uint32_t>(index)));
}

QStringList Thermal::paletteColors(int cameraType, int index) {
    if (index < 0 || index >= IMAGO_IR_PALETTE_COUNT) {
        return {QStringLiteral("#000000"), QStringLiteral("#ffffff")};
    }
    uint32_t rgb[8] = {};
    const size_t count = std::min<size_t>(
        imago_ir_palette_colors(cameraType, static_cast<uint32_t>(index), rgb, std::size(rgb)), std::size(rgb));
    QStringList colors;
    for (size_t i = 0; i < count; ++i) {
        colors.append(QStringLiteral("#%1").arg(rgb[i] & 0xFFFFFFu, 6, 16, QLatin1Char('0')));
    }
    return colors;
}

Thermal::Settings Thermal::settings(const QVariantMap& cameraValues, int streamPalette, int streamZoom,
                                    const Band& remembered) {
    Settings s;
    const int palette = value(cameraValues, "C_T_PALETTE");
    s.palette = palette >= 0 ? palette : (streamPalette >= 0 ? streamPalette : -1);
    const int zoom = value(cameraValues, "C_T_ZOOM");
    s.zoom = zoom >= 0 ? zoom + 1 : (streamZoom >= kMinZoom ? streamZoom : 0);
    const int isotherm = value(cameraValues, "C_T_ISO_M");
    s.isothermSupported = isotherm >= 0;
    s.isothermOn = isotherm == 1;
    const Band onCamera{value(cameraValues, "C_T_RG0_TMP"), value(cameraValues, "C_T_RG1_TMP")};
    s.band = s.isothermOn && onCamera.low >= 0 && onCamera.high >= 0 && isValid(onCamera) ? onCamera : remembered;
    const int gain = value(cameraValues, "C_T_G");
    s.lowGain = gain < 0 ? -1 : (gain == IMAGO_IR_GAIN_LOW ? 1 : 0);
    return s;
}

bool Thermal::followStream(QVariantMap& cameraValues, int previousPalette, int palette, int previousZoom, int zoom) {
    bool changed = false;
    if (palette >= 0 && palette != previousPalette) {
        cameraValues.insert(QStringLiteral("C_T_PALETTE"), palette);
        changed = true;
    }
    if (zoom >= kMinZoom && zoom != previousZoom) {
        cameraValues.insert(QStringLiteral("C_T_ZOOM"), zoom - 1);
        changed = true;
    }
    return changed;
}

QStringList Thermal::settingsToRead() {
    return {QStringLiteral("C_T_PALETTE"), QStringLiteral("C_T_ZOOM"),    QStringLiteral("C_T_ISO_M"),
            QStringLiteral("C_T_G"),       QStringLiteral("C_T_RG0_TMP"), QStringLiteral("C_T_RG1_TMP")};
}

std::optional<double> Thermal::fraction(double value, double minC, double maxC) {
    if (!(maxC > minC)) {
        return std::nullopt;
    }
    return std::clamp((value - minC) / (maxC - minC), 0.0, 1.0);
}

std::optional<Thermal::Span> Thermal::bandSpan(const Band& band, double minC, double maxC) {
    if (!(maxC > minC) || band.high <= minC || band.low >= maxC) {
        return std::nullopt;
    }
    return Span{*fraction(band.low, minC, maxC), *fraction(band.high, minC, maxC)};
}

QString Thermal::celsius(double value) {
    return QString::number(value, 'f', std::fabs(value) < 100.0 ? 1 : 0) + QStringLiteral("°");
}
