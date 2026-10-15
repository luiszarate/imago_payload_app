#pragma once

#include <QList>
#include <QMap>
#include <QSettings>
#include <QString>

/// What a controller button does.
enum class GamepadAction {
    Shutter,
    Record,
    StopTracking,
    ToggleDetection,
    PreviousView,
    NextView,
    Recenter,
    TogglePointMode,
    ToggleLockFollow,
    ZoomIn,
    ZoomOut,
};

/// What a controller axis drives.
enum class GamepadAxisFunction { GimbalYaw, GimbalPitch, Zoom };

/// One analog input and whether it is reversed. SDL reports stick Y axes positive down, so
/// "up" bindings are inverted.
struct GamepadAxisBinding {
    QString input;
    bool inverted = false;
    bool operator==(const GamepadAxisBinding&) const = default;
};

/// Controller inputs are named as SDL sees them:
/// - "pad:<axis or button>" on controllers SDL recognises as gamepads (Xbox, PlayStation and
///   most Bluetooth or USB pads), e.g. "pad:leftx", "pad:a". "pad:dpadx" and "pad:dpady" are
///   the D-pad as two on-or-off axes.
/// - "joy:axis<n>", "joy:button<n>", "joy:hat<n>x" / "joy:hat<n>y" on any other joystick
///   (HOTAS, flight sticks, pedals), numbered from 0.
namespace GamepadInput {
QString label(const QString& input);
/// Hats and D-pads are on or off.
bool isDigitalAxis(const QString& input);
} // namespace GamepadInput

/// Which controller inputs drive what. One mapping for the whole app, saved across launches.
struct GamepadMapping {
    QMap<GamepadAxisFunction, GamepadAxisBinding> axes;
    /// Button input -> action.
    QMap<QString, GamepadAction> buttons;

    QString button(GamepadAction action) const;

    /// One button per action and one action per button.
    GamepadMapping bound(GamepadAction action, const QString& input) const;
    GamepadMapping unbound(GamepadAction action) const;
    /// One function per analog input.
    GamepadMapping bound(GamepadAxisFunction function, const QString& input, bool inverted) const;
    GamepadMapping unbound(GamepadAxisFunction function) const;
    GamepadMapping withInverted(GamepadAxisFunction function, bool inverted) const;

    /// Left stick gimbal, right stick zoom, face buttons for the camera, shoulders for the
    /// view, Start to center the gimbal.
    static GamepadMapping standard();

    void save(QSettings& settings) const;
    static GamepadMapping load(const QSettings& settings);

    bool operator==(const GamepadMapping&) const = default;

    static QList<GamepadAction> allActions();
    static QList<GamepadAxisFunction> allFunctions();
    static QString key(GamepadAction action);
    static QString key(GamepadAxisFunction function);
    static QString title(GamepadAction action);
    static QString title(GamepadAxisFunction function);
    static bool actionFromKey(const QString& key, GamepadAction* action);
    static bool functionFromKey(const QString& key, GamepadAxisFunction* function);
    /// Acts while the button is held instead of once per press.
    static bool isHeld(GamepadAction action) {
        return action == GamepadAction::ZoomIn || action == GamepadAction::ZoomOut;
    }
};
