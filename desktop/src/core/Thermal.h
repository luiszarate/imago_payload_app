#pragma once

#include <imago/imago_core.h>

#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

/// The thermal bar's logic, as in the iOS app (ThermalBar.swift, AppModel+Thermal.swift): IR
/// palettes, the temperature scale, the isotherm band and which IR settings the camera reported.
namespace Thermal {

/// A temperature band for the isotherm, whole °C.
struct Band {
    int low = 35;
    int high = 40;
    bool operator==(const Band&) const = default;
};

struct Preset {
    QString name;
    Band band;
};

/// The camera measures 0..550 °C in our Celsius settings, high gain only up to 150 °C.
constexpr int kMinC = 0;
constexpr int kMaxC = static_cast<int>(IMAGO_IR_GAIN_LOW_MAX_C);
constexpr int kHighGainMaxC = static_cast<int>(IMAGO_IR_GAIN_HIGH_MAX_C);
/// IR digital zoom, 1..8×.
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 8;

/// From Gremsy's own examples (people) and common thermal practice (hot spots, fire). Fire goes
/// past 150 °C, so the camera drops to low gain for it.
QList<Preset> presets();
/// kMinC <= low < high <= kMaxC.
bool isValid(const Band& band);
/// The From and To steppers keep at least one degree between them.
int clampLow(int value, int high);
int clampHigh(int value, int low);

/// The last band applied from this app, else People.
Band rememberedBand(const QSettings& settings);
void rememberBand(QSettings& settings, const Band& band);

/// Palettes (C_T_PALETTE) as the core names them for the payload's sensor; "Palette N" out of
/// range.
QString paletteName(int cameraType, int index);
/// Approximate colours from cold to hot, "#rrggbb"; black to white out of range.
QStringList paletteColors(int cameraType, int index);

/// The camera's IR settings as the bar shows them: what the camera last confirmed (camera values,
/// param id -> value), else what its data stream reports.
struct Settings {
    /// C_T_PALETTE index; -1 unknown.
    int palette = -1;
    /// Digital zoom 1..8; 0 unknown.
    int zoom = 0;
    /// The camera answered a read of its isotherm switch (Gremsy's F1 thermal core has it).
    bool isothermSupported = false;
    bool isothermOn = false;
    /// The band on the camera when it is on and known, else the last one applied here.
    Band band;
    /// IR gain: -1 the camera has no gain setting, 0 high (to 150 °C), 1 low (to 550 °C).
    int lowGain = -1;
    bool operator==(const Settings&) const = default;
};
Settings settings(const QVariantMap& cameraValues, int streamPalette, int streamZoom, const Band& remembered);

/// Copies palette and zoom changes the data stream reports into the camera values, so a palette
/// changed elsewhere shows on the bar and in Settings too. true if the values changed.
bool followStream(QVariantMap& cameraValues, int previousPalette, int palette, int previousZoom, int zoom);

/// The settings the bar asks for each time the view becomes IR. The isotherm and gain ones go
/// unanswered on a camera without them.
QStringList settingsToRead();

/// Where a temperature sits on the scale, 0..1; nothing without a range.
std::optional<double> fraction(double value, double minC, double maxC);

struct Span {
    double from = 0.0;
    double to = 0.0;
};
/// The isotherm band on the scale; nothing when it lies wholly outside the picture's range.
std::optional<Span> bandSpan(const Band& band, double minC, double maxC);

/// "36.5°", or "120°" from 100 on.
QString celsius(double value);

} // namespace Thermal
