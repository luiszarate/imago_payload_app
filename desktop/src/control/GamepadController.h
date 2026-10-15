#pragma once

#include "control/GamepadMapping.h"
#include "control/GimbalInputShaper.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

/// An input the user is assigning: the next button press or axis movement goes here.
struct GamepadLearnTarget {
    enum class Kind { Action, Axis };
    Kind kind = Kind::Action;
    GamepadAction action = GamepadAction::Shutter;
    GamepadAxisFunction function = GamepadAxisFunction::GimbalYaw;

    /// "action:shutter", "axis:gimbalYaw": how QML names it.
    QString key() const;
    static std::optional<GamepadLearnTarget> fromKey(const QString& key);
    bool operator==(const GamepadLearnTarget&) const = default;
};

/// Physical controllers through SDL2: Xbox, PlayStation and generic Bluetooth or USB gamepads,
/// and HOTAS, flight sticks or pedals (any HID joystick). Every axis and button can be
/// assigned; what each one does comes from a GamepadMapping the user edits.
///
/// SDL is polled on the main thread every few milliseconds, also while the window is in the
/// background. Gimbal rates and zoom go straight to the callbacks, which send them to the core.
class GamepadController : public QObject {
    Q_OBJECT

  public:
    explicit GamepadController(QObject* parent = nullptr);
    ~GamepadController() override;

    /// Gimbal rates in degrees per second.
    std::function<void(float pitch, float yaw)> onGimbalRate;
    /// -1 zoom out, 0 stop, 1 zoom in.
    std::function<void(int direction)> onZoom;
    /// Actions that are not held (zoom goes through onZoom).
    std::function<void(GamepadAction)> onAction;
    /// What was pressed or moved while learning: the input and, for an axis, whether it moved
    /// toward its negative end.
    std::function<void(const GamepadLearnTarget&, const QString& input, bool inverted)> onLearned;
    /// The first controller's name, or empty when none is connected.
    std::function<void(const QString& name)> onConnectionChange;

    /// Latest EO zoom, used to scale the gimbal rate.
    float zoomLevel = 1.0f;

    bool start();
    void stop();

    void setMapping(const GamepadMapping& mapping);
    void setShaper(const GimbalInputShaper& shaper);
    void setLearning(std::optional<GamepadLearnTarget> target);

    /// Releases everything: never leave the gimbal or the zoom moving.
    void stopAll();

  private:
    void poll();
    void deviceAdded(int index);
    void deviceRemoved(int instanceId);
    void devicesChanged();
    void axisMoved(const QString& input, float value);
    void buttonChanged(const QString& input, bool down);
    void hatMoved(int instanceId, int hat, int value);
    void updateAxes();
    float value(const GamepadAxisBinding* binding) const;
    void zoomButton(int direction, bool down);
    void zoomAxisChanged(float value);
    void setZoom(int direction);

    QTimer timer_;
    bool started_ = false;
    GimbalInputShaper shaper_;
    GamepadMapping mapping_ = GamepadMapping::standard();
    std::optional<GamepadLearnTarget> learning_;

    struct Device {
        void* handle = nullptr; // SDL_GameController* or SDL_Joystick*
        bool gamepad = false;
        QString name;
    };
    QHash<int, Device> devices_; // by SDL instance ID, in connection order
    QList<int> order_;
    QString connectedName_;

    int zoomDirection_ = 0;
    int zoomButtonDirection_ = 0;
    QSet<QString> pressed_;
    GimbalInputShaper::Rates lastRates_;
    /// Latest value of every axis, so a button event does not reset the sticks.
    QHash<QString, float> axisValues_;
    /// Axis positions when learning began: throttles and pedals rest away from zero.
    QHash<QString, float> learnBaseline_;
    /// D-pad buttons held, for the "pad:dpadx/y" axes.
    int dpadX_ = 0;
    int dpadY_ = 0;
};
