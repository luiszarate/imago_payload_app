#include "core/CameraSettings.h"

#include "core/Thermal.h"

#include <QVariantMap>

namespace {

/// Most Orus L switches use 2 = on, 3 = off.
CameraSetting toggle(const char* id, const char* title, int on = 2, int off = 3) {
    CameraSetting s;
    s.id = QString::fromLatin1(id);
    s.title = QString::fromUtf8(title);
    s.toggle = true;
    s.on = on;
    s.off = off;
    return s;
}

CameraSetting choice(const char* id, const char* title, QList<CameraSetting::Option> options,
                     const char* note = nullptr) {
    CameraSetting s;
    s.id = QString::fromLatin1(id);
    s.title = QString::fromUtf8(title);
    s.options = std::move(options);
    if (note != nullptr) {
        s.note = QString::fromUtf8(note);
    }
    return s;
}

/// value 0 until count, shown as 1, 2, 3... with an optional suffix.
CameraSetting steps(const char* id, const char* title, int count, const char* suffix = "") {
    QList<CameraSetting::Option> options;
    for (int i = 0; i < count; ++i) {
        options.append({i, QString::number(i + 1) + QString::fromUtf8(suffix)});
    }
    return choice(id, title, options);
}

QList<CameraSettingSection> image() {
    return {
        {QStringLiteral("zoom"), QStringLiteral("Zoom"),
         {
             choice("C_V_ZM_MODE", "Zoom mode", {{0, QStringLiteral("Combined · 300×")}, {2, QStringLiteral("Super resolution · 25×")}}),
             steps("C_V_Z_SPD", "Zoom speed", 8),
             toggle("C_V_STZOOM", "Stable zoom", 1, 0),
         },
         QStringLiteral("Zoom speed applies to the + and − buttons, the mouse wheel and the gamepad.")},
        {QStringLiteral("focus"), QStringLiteral("Focus"),
         {
             choice("C_V_FM", "Focus mode", {{1, QStringLiteral("Auto")}, {0, QStringLiteral("Manual")}, {2, QStringLiteral("One-push")}}),
             steps("C_V_F_SPD", "Focus speed", 8),
         },
         {}},
        {QStringLiteral("image"), QStringLiteral("Image"),
         {
             toggle("C_V_DEFOG", "Defog"),
             steps("C_V_DEFOG_LV", "Defog level", 4),
             choice("C_V_EIS", "Stabilization (EIS)", {{2, QStringLiteral("On")}, {3, QStringLiteral("Off")}, {0, QStringLiteral("Hold")}}),
             choice("C_V_EIS_LV", "Stabilization level", {{2, QStringLiteral("Super")}, {3, QStringLiteral("Super+")}}),
             choice("C_V_NSREDUCT", "Noise reduction",
                    {{0, QStringLiteral("Off")}, {1, QStringLiteral("1")}, {2, QStringLiteral("2")},
                     {3, QStringLiteral("3")}, {4, QStringLiteral("4")}, {5, QStringLiteral("5")}}),
             toggle("C_V_FLREDUCT", "Flicker reduction"),
             toggle("C_V_FLIP", "Flip image"),
         },
         {}},
        {QStringLiteral("night"), QStringLiteral("Day / night"),
         {
             choice("C_V_ICR", "IR-cut filter", {{2, QStringLiteral("Auto")}, {3, QStringLiteral("Manual")}}),
             choice("C_V_ICR_MAN", "IR-cut (manual)", {{3, QStringLiteral("Day")}, {2, QStringLiteral("Night")}},
                    "Only with the IR-cut filter on manual."),
             choice("C_F_DEFOG", "Lens heater", {{0, QStringLiteral("Auto")}, {2, QStringLiteral("On")}, {1, QStringLiteral("Off")}}),
         },
         {}},
        {QStringLiteral("osd"), QStringLiteral("Overlay"),
         {
             choice("OSD_MODE", "Payload OSD in the video", {{0, QStringLiteral("Off")}, {2, QStringLiteral("Status")}, {1, QStringLiteral("Debug")}}),
         },
         QStringLiteral("The text the payload draws into the picture itself (and into recordings).")},
    };
}

QList<CameraSettingSection> infrared() {
    QList<CameraSetting::Option> palettes;
    for (int i = 0; i < IMAGO_IR_PALETTE_COUNT; ++i) {
        palettes.append({i, Thermal::paletteName(-1, i)});
    }
    return {
        {QStringLiteral("ir"), QStringLiteral("Thermal"),
         {
             choice("C_T_PALETTE", "Palette", palettes),
             steps("C_T_ZOOM", "Digital zoom", Thermal::kMaxZoom, "×"),
         },
         QStringLiteral("Also on the thermal bar under the video while the view is IR or IR + EO.")},
    };
}

QList<CameraSettingSection> gimbal() {
    return {
        {QStringLiteral("gimbal"), QStringLiteral("Gimbal"),
         {
             choice("GB_MODE", "Mode", {{1, QStringLiteral("Lock")}, {2, QStringLiteral("Follow")}, {3, QStringLiteral("Mapping")}, {0, QStringLiteral("Off")}}),
             choice("RC_MODE", "RC input", {{0, QStringLiteral("Gremsy")}, {1, QStringLiteral("Standard")}},
                    "How the pilot's RC drives the gimbal."),
         },
         {}},
    };
}

} // namespace

QStringList CameraSettingsCatalog::pageTitles() {
    return {QStringLiteral("Camera"), QStringLiteral("Thermal & stream"), QStringLiteral("Gimbal"),
            QStringLiteral("Controller")};
}

QList<CameraSettingSection> CameraSettingsCatalog::sections(int page) {
    switch (page) {
    case CameraPage:
        return image();
    case ThermalPage:
        return infrared();
    case GimbalPage:
        return gimbal();
    default:
        return {};
    }
}

QVariantList CameraSettingsCatalog::sectionsVariant(int page) {
    QVariantList result;
    for (const CameraSettingSection& section : sections(page)) {
        QVariantList settings;
        for (const CameraSetting& setting : section.settings) {
            QVariantList options;
            if (setting.toggle) {
                options = {QVariantMap{{QStringLiteral("value"), setting.on}, {QStringLiteral("label"), QStringLiteral("On")}},
                           QVariantMap{{QStringLiteral("value"), setting.off}, {QStringLiteral("label"), QStringLiteral("Off")}}};
            }
            for (const CameraSetting::Option& option : setting.options) {
                options.append(QVariantMap{{QStringLiteral("value"), option.value}, {QStringLiteral("label"), option.label}});
            }
            settings.append(QVariantMap{
                {QStringLiteral("id"), setting.id},
                {QStringLiteral("title"), setting.title},
                {QStringLiteral("note"), setting.note},
                {QStringLiteral("toggle"), setting.toggle},
                {QStringLiteral("on"), setting.on},
                {QStringLiteral("off"), setting.off},
                {QStringLiteral("options"), options},
            });
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), section.id},
            {QStringLiteral("title"), section.title},
            {QStringLiteral("footer"), section.footer},
            {QStringLiteral("settings"), settings},
        });
    }
    return result;
}

QList<int> CameraSettingsCatalog::bitrates() {
    QList<int> result;
    for (const int mbps : {1, 2, 3, 4, 6, 8}) {
        result.append(mbps * 1'000'000);
    }
    return result;
}
