#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

/// A setting the payload keeps, written with PARAM_EXT_SET and read with PARAM_EXT_REQUEST_READ
/// (values in docs/protocol/orus-l.md).
struct CameraSetting {
    struct Option {
        int value;
        QString label;
    };

    /// The param_id.
    QString id;
    QString title;
    /// A switch (on/off values) or a menu of options.
    bool toggle = false;
    int on = 2;
    int off = 3;
    QList<Option> options;
    QString note;
};

struct CameraSettingSection {
    QString id;
    QString title;
    QList<CameraSetting> settings;
    QString footer;
};

/// The settings the app shows, grouped as on the settings screen.
namespace CameraSettingsCatalog {

enum Page { CameraPage = 0, ThermalPage = 1, GimbalPage = 2, ControllerPage = 3 };

QStringList pageTitles();
QList<CameraSettingSection> sections(int page);
/// For QML: [{id, title, footer, settings: [{id, title, note, toggle, on, off, options: [{value, label}]}]}].
QVariantList sectionsVariant(int page);
/// Stream bitrates offered, bit/s.
QList<int> bitrates();

} // namespace CameraSettingsCatalog
