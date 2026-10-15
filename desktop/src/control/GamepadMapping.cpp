#include "control/GamepadMapping.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const char* kKey = "controls.gamepadMapping";

struct ActionInfo {
    GamepadAction action;
    const char* key;
    const char* title;
};

constexpr ActionInfo kActions[] = {
    {GamepadAction::Shutter, "shutter", "Shutter (photo or record)"},
    {GamepadAction::Record, "record", "Start / stop recording"},
    {GamepadAction::StopTracking, "stopTracking", "Stop tracking"},
    {GamepadAction::ToggleDetection, "toggleDetection", "AI detection on / off"},
    {GamepadAction::PreviousView, "previousView", "Previous view"},
    {GamepadAction::NextView, "nextView", "Next view"},
    {GamepadAction::Recenter, "recenter", "Center gimbal"},
    {GamepadAction::TogglePointMode, "togglePointMode", "Point mode on / off"},
    {GamepadAction::ToggleLockFollow, "toggleLockFollow", "Gimbal lock / follow"},
    {GamepadAction::ZoomIn, "zoomIn", "Zoom in (hold)"},
    {GamepadAction::ZoomOut, "zoomOut", "Zoom out (hold)"},
};

struct FunctionInfo {
    GamepadAxisFunction function;
    const char* key;
    const char* title;
};

constexpr FunctionInfo kFunctions[] = {
    {GamepadAxisFunction::GimbalYaw, "gimbalYaw", "Gimbal pan (yaw)"},
    {GamepadAxisFunction::GimbalPitch, "gimbalPitch", "Gimbal tilt (pitch)"},
    {GamepadAxisFunction::Zoom, "zoom", "Zoom"},
};

const QHash<QString, QString>& padLabels() {
    static const QHash<QString, QString> labels = {
        {QStringLiteral("leftx"), QStringLiteral("Left stick ←→")},
        {QStringLiteral("lefty"), QStringLiteral("Left stick ↑↓")},
        {QStringLiteral("rightx"), QStringLiteral("Right stick ←→")},
        {QStringLiteral("righty"), QStringLiteral("Right stick ↑↓")},
        {QStringLiteral("lefttrigger"), QStringLiteral("Left trigger")},
        {QStringLiteral("righttrigger"), QStringLiteral("Right trigger")},
        {QStringLiteral("dpadx"), QStringLiteral("D-pad ←→")},
        {QStringLiteral("dpady"), QStringLiteral("D-pad ↑↓")},
        {QStringLiteral("a"), QStringLiteral("A")},
        {QStringLiteral("b"), QStringLiteral("B")},
        {QStringLiteral("x"), QStringLiteral("X")},
        {QStringLiteral("y"), QStringLiteral("Y")},
        {QStringLiteral("back"), QStringLiteral("Back")},
        {QStringLiteral("guide"), QStringLiteral("Guide")},
        {QStringLiteral("start"), QStringLiteral("Start")},
        {QStringLiteral("leftstick"), QStringLiteral("Left stick press")},
        {QStringLiteral("rightstick"), QStringLiteral("Right stick press")},
        {QStringLiteral("leftshoulder"), QStringLiteral("Left bumper")},
        {QStringLiteral("rightshoulder"), QStringLiteral("Right bumper")},
        {QStringLiteral("dpup"), QStringLiteral("D-pad ↑")},
        {QStringLiteral("dpdown"), QStringLiteral("D-pad ↓")},
        {QStringLiteral("dpleft"), QStringLiteral("D-pad ←")},
        {QStringLiteral("dpright"), QStringLiteral("D-pad →")},
        {QStringLiteral("misc1"), QStringLiteral("Share")},
        {QStringLiteral("touchpad"), QStringLiteral("Touchpad")},
    };
    return labels;
}

} // namespace

QString GamepadInput::label(const QString& input) {
    if (input.startsWith(QLatin1String("pad:"))) {
        const QString name = input.mid(4);
        const auto it = padLabels().find(name);
        if (it != padLabels().end()) {
            return *it;
        }
        static const QRegularExpression paddle(QStringLiteral("^paddle(\\d)$"));
        if (const auto m = paddle.match(name); m.hasMatch()) {
            return QStringLiteral("Paddle %1").arg(m.captured(1));
        }
        return name;
    }
    static const QRegularExpression joy(QStringLiteral("^joy:(axis|button|hat)(\\d+)([xy]?)$"));
    if (const auto m = joy.match(input); m.hasMatch()) {
        const int number = m.captured(2).toInt() + 1;
        const QString kind = m.captured(1);
        if (kind == QLatin1String("axis")) {
            return QStringLiteral("Axis %1").arg(number);
        }
        if (kind == QLatin1String("button")) {
            return QStringLiteral("Button %1").arg(number);
        }
        return QStringLiteral("Hat %1 %2").arg(number).arg(m.captured(3) == QLatin1String("x")
                                                               ? QStringLiteral("←→")
                                                               : QStringLiteral("↑↓"));
    }
    return input;
}

bool GamepadInput::isDigitalAxis(const QString& input) {
    return input == QLatin1String("pad:dpadx") || input == QLatin1String("pad:dpady") ||
           input.startsWith(QLatin1String("joy:hat"));
}

// ----------------------------------------------------------------------------------------------

QList<GamepadAction> GamepadMapping::allActions() {
    QList<GamepadAction> all;
    for (const auto& info : kActions) {
        all.append(info.action);
    }
    return all;
}

QList<GamepadAxisFunction> GamepadMapping::allFunctions() {
    QList<GamepadAxisFunction> all;
    for (const auto& info : kFunctions) {
        all.append(info.function);
    }
    return all;
}

QString GamepadMapping::key(GamepadAction action) {
    for (const auto& info : kActions) {
        if (info.action == action) return QString::fromLatin1(info.key);
    }
    return {};
}

QString GamepadMapping::key(GamepadAxisFunction function) {
    for (const auto& info : kFunctions) {
        if (info.function == function) return QString::fromLatin1(info.key);
    }
    return {};
}

QString GamepadMapping::title(GamepadAction action) {
    for (const auto& info : kActions) {
        if (info.action == action) return QString::fromUtf8(info.title);
    }
    return {};
}

QString GamepadMapping::title(GamepadAxisFunction function) {
    for (const auto& info : kFunctions) {
        if (info.function == function) return QString::fromUtf8(info.title);
    }
    return {};
}

bool GamepadMapping::actionFromKey(const QString& key, GamepadAction* action) {
    for (const auto& info : kActions) {
        if (key == QLatin1String(info.key)) {
            *action = info.action;
            return true;
        }
    }
    return false;
}

bool GamepadMapping::functionFromKey(const QString& key, GamepadAxisFunction* function) {
    for (const auto& info : kFunctions) {
        if (key == QLatin1String(info.key)) {
            *function = info.function;
            return true;
        }
    }
    return false;
}

QString GamepadMapping::button(GamepadAction action) const {
    for (auto it = buttons.begin(); it != buttons.end(); ++it) {
        if (it.value() == action) return it.key();
    }
    return {};
}

GamepadMapping GamepadMapping::bound(GamepadAction action, const QString& input) const {
    GamepadMapping copy = unbound(action);
    copy.buttons.insert(input, action);
    return copy;
}

GamepadMapping GamepadMapping::unbound(GamepadAction action) const {
    GamepadMapping copy = *this;
    copy.buttons.removeIf([action](const auto& entry) { return entry.value() == action; });
    return copy;
}

GamepadMapping GamepadMapping::bound(GamepadAxisFunction function, const QString& input,
                                     bool inverted) const {
    GamepadMapping copy = *this;
    copy.axes.removeIf([&input](const auto& entry) { return entry.value().input == input; });
    copy.axes.insert(function, GamepadAxisBinding{input, inverted});
    return copy;
}

GamepadMapping GamepadMapping::unbound(GamepadAxisFunction function) const {
    GamepadMapping copy = *this;
    copy.axes.remove(function);
    return copy;
}

GamepadMapping GamepadMapping::withInverted(GamepadAxisFunction function, bool inverted) const {
    GamepadMapping copy = *this;
    if (copy.axes.contains(function)) {
        copy.axes[function].inverted = inverted;
    }
    return copy;
}

GamepadMapping GamepadMapping::standard() {
    GamepadMapping m;
    m.axes = {
        {GamepadAxisFunction::GimbalYaw, {QStringLiteral("pad:leftx"), false}},
        {GamepadAxisFunction::GimbalPitch, {QStringLiteral("pad:lefty"), true}},
        {GamepadAxisFunction::Zoom, {QStringLiteral("pad:righty"), true}},
    };
    m.buttons = {
        {QStringLiteral("pad:a"), GamepadAction::Shutter},
        {QStringLiteral("pad:b"), GamepadAction::Record},
        {QStringLiteral("pad:x"), GamepadAction::StopTracking},
        {QStringLiteral("pad:y"), GamepadAction::ToggleDetection},
        {QStringLiteral("pad:leftshoulder"), GamepadAction::PreviousView},
        {QStringLiteral("pad:rightshoulder"), GamepadAction::NextView},
        {QStringLiteral("pad:start"), GamepadAction::Recenter},
    };
    return m;
}

void GamepadMapping::save(QSettings& settings) const {
    QJsonObject axesJson;
    for (auto it = axes.begin(); it != axes.end(); ++it) {
        axesJson.insert(key(it.key()), QJsonObject{{QStringLiteral("input"), it.value().input},
                                                    {QStringLiteral("inverted"), it.value().inverted}});
    }
    QJsonObject buttonsJson;
    for (auto it = buttons.begin(); it != buttons.end(); ++it) {
        buttonsJson.insert(it.key(), key(it.value()));
    }
    const QJsonObject json{{QStringLiteral("axes"), axesJson}, {QStringLiteral("buttons"), buttonsJson}};
    settings.setValue(QLatin1String(kKey),
                      QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
}

GamepadMapping GamepadMapping::load(const QSettings& settings) {
    const QByteArray stored = settings.value(QLatin1String(kKey)).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(stored);
    if (!doc.isObject()) {
        return standard();
    }
    GamepadMapping m;
    const QJsonObject axesJson = doc.object().value(QStringLiteral("axes")).toObject();
    for (auto it = axesJson.begin(); it != axesJson.end(); ++it) {
        GamepadAxisFunction function;
        const QJsonObject binding = it.value().toObject();
        const QString input = binding.value(QStringLiteral("input")).toString();
        if (functionFromKey(it.key(), &function) && !input.isEmpty()) {
            m.axes.insert(function, {input, binding.value(QStringLiteral("inverted")).toBool()});
        }
    }
    const QJsonObject buttonsJson = doc.object().value(QStringLiteral("buttons")).toObject();
    for (auto it = buttonsJson.begin(); it != buttonsJson.end(); ++it) {
        GamepadAction action;
        if (actionFromKey(it.value().toString(), &action)) {
            m.buttons.insert(it.key(), action);
        }
    }
    return m;
}
