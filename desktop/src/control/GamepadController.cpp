#include "control/GamepadController.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace {

/// Deflection that counts as "this input" while learning.
constexpr float kLearnThreshold = 0.6f;
/// SDL axes never rest exactly at zero; this much is noise.
constexpr float kFlat = 0.02f;
/// Polling period: input reaches the core within this, plus the OS's own delay.
constexpr int kPollMs = 4;

float normalized(Sint16 value) {
    const float v = std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
    return std::fabs(v) <= kFlat ? 0.0f : v;
}

QString padName(const char* name) { return QStringLiteral("pad:") + QString::fromLatin1(name); }

} // namespace

QString GamepadLearnTarget::key() const {
    return kind == Kind::Action ? QStringLiteral("action:") + GamepadMapping::key(action)
                                : QStringLiteral("axis:") + GamepadMapping::key(function);
}

std::optional<GamepadLearnTarget> GamepadLearnTarget::fromKey(const QString& key) {
    GamepadLearnTarget target;
    if (key.startsWith(QLatin1String("action:")) &&
        GamepadMapping::actionFromKey(key.mid(7), &target.action)) {
        target.kind = Kind::Action;
        return target;
    }
    if (key.startsWith(QLatin1String("axis:")) &&
        GamepadMapping::functionFromKey(key.mid(5), &target.function)) {
        target.kind = Kind::Axis;
        return target;
    }
    return std::nullopt;
}

// ----------------------------------------------------------------------------------------------

GamepadController::GamepadController(QObject* parent) : QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &GamepadController::poll);
}

GamepadController::~GamepadController() { stop(); }

bool GamepadController::start() {
    if (started_) {
        return true;
    }
    // Controllers keep working while another window has the focus (a map, a ground station).
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        qWarning() << "Controllers unavailable:" << SDL_GetError();
        return false;
    }
    // Devices already plugged in arrive as SDL_JOYDEVICEADDED events on the first poll.
    started_ = true;
    timer_.start();
    return true;
}

void GamepadController::stop() {
    if (!started_) {
        return;
    }
    stopAll();
    timer_.stop();
    for (const Device& device : std::as_const(devices_)) {
        if (device.gamepad) {
            SDL_GameControllerClose(static_cast<SDL_GameController*>(device.handle));
        } else {
            SDL_JoystickClose(static_cast<SDL_Joystick*>(device.handle));
        }
    }
    devices_.clear();
    order_.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_Quit();
    started_ = false;
}

// MARK: Configuration
//
// Each change stops whatever the controller is driving: a held button or stick may no longer
// be mapped, or its release may go to learning, and neither must leave the gimbal or the zoom
// moving. The next input starts it again with the new settings.

void GamepadController::setMapping(const GamepadMapping& mapping) {
    mapping_ = mapping;
    stopAll();
}

void GamepadController::setShaper(const GimbalInputShaper& shaper) {
    shaper_ = shaper;
    stopAll();
}

void GamepadController::setLearning(std::optional<GamepadLearnTarget> target) {
    learning_ = target;
    learnBaseline_.clear();
    stopAll();
}

void GamepadController::stopAll() {
    lastRates_ = {};
    if (onGimbalRate) {
        onGimbalRate(0.0f, 0.0f);
    }
    pressed_.clear();
    axisValues_.clear();
    dpadX_ = 0;
    dpadY_ = 0;
    zoomButtonDirection_ = 0;
    setZoom(0);
}

// MARK: Devices

void GamepadController::deviceAdded(int index) {
    Device device;
    SDL_Joystick* joystick = nullptr;
    if (SDL_IsGameController(index)) {
        auto* controller = SDL_GameControllerOpen(index);
        if (controller == nullptr) {
            return;
        }
        device.handle = controller;
        device.gamepad = true;
        device.name = QString::fromUtf8(SDL_GameControllerName(controller));
        joystick = SDL_GameControllerGetJoystick(controller);
    } else {
        joystick = SDL_JoystickOpen(index);
        if (joystick == nullptr) {
            return;
        }
        device.handle = joystick;
        device.name = QString::fromUtf8(SDL_JoystickName(joystick));
    }
    if (device.name.isEmpty()) {
        device.name = device.gamepad ? QStringLiteral("Gamepad") : QStringLiteral("Joystick");
    }
    const int id = SDL_JoystickInstanceID(joystick);
    devices_.insert(id, device);
    order_.removeAll(id);
    order_.append(id);
    devicesChanged();
}

void GamepadController::deviceRemoved(int instanceId) {
    const auto it = devices_.find(instanceId);
    if (it == devices_.end()) {
        return;
    }
    if (it->gamepad) {
        SDL_GameControllerClose(static_cast<SDL_GameController*>(it->handle));
    } else {
        SDL_JoystickClose(static_cast<SDL_Joystick*>(it->handle));
    }
    devices_.erase(it);
    order_.removeAll(instanceId);
    stopAll(); // never leave the gimbal or the zoom moving
    devicesChanged();
}

void GamepadController::devicesChanged() {
    const QString name = order_.isEmpty() ? QString() : devices_.value(order_.first()).name;
    if (name != connectedName_) {
        connectedName_ = name;
        if (onConnectionChange) {
            onConnectionChange(name);
        }
    }
}

// MARK: Input

void GamepadController::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
        case SDL_JOYDEVICEADDED:
            deviceAdded(event.jdevice.which);
            break;
        case SDL_JOYDEVICEREMOVED:
            deviceRemoved(event.jdevice.which);
            break;
        case SDL_CONTROLLERAXISMOTION: {
            const char* name =
                SDL_GameControllerGetStringForAxis(static_cast<SDL_GameControllerAxis>(event.caxis.axis));
            if (name != nullptr) {
                axisMoved(padName(name), normalized(event.caxis.value));
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
            const auto button = static_cast<SDL_GameControllerButton>(event.cbutton.button);
            const char* name = SDL_GameControllerGetStringForButton(button);
            if (name == nullptr) {
                break;
            }
            // The D-pad is also two on-or-off axes, so it can steer the gimbal.
            int* dpad = nullptr;
            int direction = 0;
            switch (button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                dpad = &dpadX_;
                direction = -1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                dpad = &dpadX_;
                direction = 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                dpad = &dpadY_;
                direction = -1; // like a stick: up is negative
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                dpad = &dpadY_;
                direction = 1;
                break;
            default:
                break;
            }
            const QString axis = dpad == &dpadX_ ? QStringLiteral("pad:dpadx") : QStringLiteral("pad:dpady");
            if (learning_) {
                // One input per assignment: an axis learns the D-pad, an action the button.
                if (learning_->kind == GamepadLearnTarget::Kind::Axis && dpad != nullptr && down) {
                    axisMoved(axis, static_cast<float>(direction));
                } else {
                    buttonChanged(padName(name), down);
                }
                break;
            }
            if (dpad != nullptr) {
                if (down) {
                    *dpad = direction;
                } else if (*dpad == direction) {
                    *dpad = 0;
                }
                axisMoved(axis, static_cast<float>(*dpad));
            }
            buttonChanged(padName(name), down);
            break;
        }
        case SDL_JOYAXISMOTION:
            // Gamepads report through the SDL_CONTROLLER events above.
            if (devices_.contains(event.jaxis.which) && !devices_.value(event.jaxis.which).gamepad) {
                axisMoved(QStringLiteral("joy:axis%1").arg(event.jaxis.axis), normalized(event.jaxis.value));
            }
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            if (devices_.contains(event.jbutton.which) && !devices_.value(event.jbutton.which).gamepad) {
                buttonChanged(QStringLiteral("joy:button%1").arg(event.jbutton.button),
                              event.type == SDL_JOYBUTTONDOWN);
            }
            break;
        case SDL_JOYHATMOTION:
            if (devices_.contains(event.jhat.which) && !devices_.value(event.jhat.which).gamepad) {
                hatMoved(event.jhat.which, event.jhat.hat, event.jhat.value);
            }
            break;
        default:
            break;
        }
    }
}

void GamepadController::hatMoved(int /*instanceId*/, int hat, int value) {
    const float x = (value & SDL_HAT_LEFT) != 0 ? -1.0f : ((value & SDL_HAT_RIGHT) != 0 ? 1.0f : 0.0f);
    const float y = (value & SDL_HAT_UP) != 0 ? -1.0f : ((value & SDL_HAT_DOWN) != 0 ? 1.0f : 0.0f);
    const QString base = QStringLiteral("joy:hat%1").arg(hat);
    if (learning_) {
        // Only the direction that moved counts.
        if (x != 0.0f) {
            axisMoved(base + QLatin1Char('x'), x);
        } else if (y != 0.0f) {
            axisMoved(base + QLatin1Char('y'), y);
        }
        return;
    }
    axisValues_.insert(base + QLatin1Char('x'), x);
    axisMoved(base + QLatin1Char('y'), y);
}

void GamepadController::axisMoved(const QString& input, float value) {
    if (learning_) {
        if (learning_->kind != GamepadLearnTarget::Kind::Axis) {
            return;
        }
        // The axis moved far enough from where it was when learning began.
        if (!learnBaseline_.contains(input)) {
            learnBaseline_.insert(input, std::fabs(value) < kLearnThreshold ? 0.0f : value);
        }
        const float delta = value - learnBaseline_.value(input);
        if (std::fabs(delta) < kLearnThreshold) {
            return;
        }
        const GamepadLearnTarget target = *learning_;
        learning_.reset();
        if (onLearned) {
            onLearned(target, input, delta < 0);
        }
        return;
    }
    axisValues_.insert(input, value);
    updateAxes();
}

void GamepadController::buttonChanged(const QString& input, bool down) {
    if (learning_) {
        if (learning_->kind == GamepadLearnTarget::Kind::Action && down) {
            const GamepadLearnTarget target = *learning_;
            learning_.reset();
            if (onLearned) {
                onLearned(target, input, false);
            }
        }
        return;
    }
    const auto it = mapping_.buttons.find(input);
    if (it == mapping_.buttons.end()) {
        return;
    }
    const bool wasDown = pressed_.contains(input);
    if (down == wasDown) {
        return;
    }
    if (down) {
        pressed_.insert(input);
    } else {
        pressed_.remove(input);
    }
    switch (it.value()) {
    case GamepadAction::ZoomIn:
        zoomButton(1, down);
        break;
    case GamepadAction::ZoomOut:
        zoomButton(-1, down);
        break;
    default:
        if (down && onAction) {
            onAction(it.value());
        }
        break;
    }
}

void GamepadController::updateAxes() {
    const auto yaw = mapping_.axes.constFind(GamepadAxisFunction::GimbalYaw);
    const auto pitch = mapping_.axes.constFind(GamepadAxisFunction::GimbalPitch);
    const GamepadAxisBinding* yawBinding = yaw != mapping_.axes.cend() ? &yaw.value() : nullptr;
    const GamepadAxisBinding* pitchBinding = pitch != mapping_.axes.cend() ? &pitch.value() : nullptr;
    // Stick Y is positive down; the shaper takes up as positive, and "up" bindings are inverted.
    const GimbalInputShaper::Rates rates = shaper_.rates(
        value(yawBinding), value(pitchBinding), zoomLevel,
        yawBinding != nullptr && GamepadInput::isDigitalAxis(yawBinding->input),
        pitchBinding != nullptr && GamepadInput::isDigitalAxis(pitchBinding->input));
    if (!(rates == lastRates_)) {
        lastRates_ = rates;
        if (onGimbalRate) {
            onGimbalRate(rates.pitch, rates.yaw);
        }
    }
    // Unbound means centred, so a zoom running from an axis that was just cleared stops.
    const auto zoom = mapping_.axes.constFind(GamepadAxisFunction::Zoom);
    zoomAxisChanged(value(zoom != mapping_.axes.cend() ? &zoom.value() : nullptr));
}

float GamepadController::value(const GamepadAxisBinding* binding) const {
    if (binding == nullptr) {
        return 0.0f;
    }
    const float raw = axisValues_.value(binding->input, 0.0f);
    return binding->inverted ? -raw : raw;
}

void GamepadController::zoomButton(int direction, bool down) {
    if (down) {
        zoomButtonDirection_ = direction;
        setZoom(direction);
    } else if (zoomButtonDirection_ == direction) {
        zoomButtonDirection_ = 0;
        setZoom(0);
    }
}

void GamepadController::zoomAxisChanged(float v) {
    if (zoomButtonDirection_ != 0) {
        return; // a held zoom button wins
    }
    // Hysteresis: start at 0.5, stop below 0.3, so the zoom does not chatter.
    int direction = zoomDirection_;
    if (v > 0.5f) {
        direction = 1;
    } else if (v < -0.5f) {
        direction = -1;
    } else if (std::fabs(v) < 0.3f) {
        direction = 0;
    }
    setZoom(direction);
}

void GamepadController::setZoom(int direction) {
    if (direction != zoomDirection_) {
        zoomDirection_ = direction;
        if (onZoom) {
            onZoom(direction);
        }
    }
}
