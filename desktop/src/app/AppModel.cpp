#include "app/AppModel.h"

#include "app/SystemIntegration.h"
#include "core/CameraSettings.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <QVideoSink>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPollIntervalMs = 33;
const char* kShowAttitudeKey = "ui.showAttitude";

/// Commands that change what the camera does, worth a warning when someone else sends them.
bool isCameraCommand(int command) {
    switch (command) {
    case 530:
    case 531:
    case 532:
    case 2000:
    case 2001:
    case 2500:
    case 2501:
    case 31013:
        return true;
    default:
        return false;
    }
}

QString commandName(int command) {
    switch (command) {
    case 530:
        return QStringLiteral("Camera mode");
    case 531:
        return QStringLiteral("Zoom");
    case 532:
        return QStringLiteral("Focus");
    case 2000:
        return QStringLiteral("Photo");
    case 2500:
        return QStringLiteral("Start recording");
    case 2501:
        return QStringLiteral("Stop recording");
    case 31013:
        return QStringLiteral("Tracking");
    default:
        return QStringLiteral("Command %1").arg(command);
    }
}

QString paramName(const QString& id) {
    if (id == QLatin1String("GB_MODE")) return QStringLiteral("Gimbal mode");
    if (id == QLatin1String("C_SOURCE")) return QStringLiteral("View");
    if (id == QLatin1String("TRACK_MODE")) return QStringLiteral("AI detection");
    if (id == QLatin1String("RC_MODE")) return QStringLiteral("RC mode");
    return id;
}

/// "system/component" as people know it.
QString senderName(const QString& from) {
    const QStringList parts = from.split(QLatin1Char('/'));
    bool okSystem = false;
    bool okComponent = false;
    const int system = parts.size() == 2 ? parts[0].toInt(&okSystem) : 0;
    const int component = parts.size() == 2 ? parts[1].toInt(&okComponent) : 0;
    if (!okSystem || !okComponent) {
        return from;
    }
    if (component == 1) {
        return QStringLiteral("the autopilot (%1)").arg(from);
    }
    if (system == 255 || component == 190) {
        return QStringLiteral("a ground station (%1)").arg(from);
    }
    return QStringLiteral("another controller (%1)").arg(from);
}

QVariantMap option(const QVariant& value, const QString& title) {
    return {{QStringLiteral("value"), value}, {QStringLiteral("title"), title}};
}

AppModel* gInstance = nullptr;

} // namespace

AppModel* AppModel::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT(gInstance != nullptr);
    QJSEngine::setObjectOwnership(gInstance, QJSEngine::CppOwnership);
    return gInstance;
}

void AppModel::setInstance(AppModel* model) { gInstance = model; }

AppModel::AppModel(QObject* parent)
    : QObject(parent),
      settings_(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Imago Aerospace"),
                QStringLiteral("Imago Payload Control")),
      store_(settings_) {
    profiles_ = store_.load();
    // Persist the built-in profiles once, so their IDs (and the selection) survive relaunch.
    store_.save(profiles_);
    const QString stored = store_.selectedId();
    const bool known = std::any_of(profiles_.begin(), profiles_.end(),
                                   [&stored](const ConnectionProfile& p) { return p.id == stored; });
    selectedProfileId_ = known ? stored : (profiles_.isEmpty() ? QString() : profiles_.first().id);

    showAttitude_ = settings_.value(QLatin1String(kShowAttitudeKey), true).toBool();
    gimbalTuning_ = GimbalInputShaper::load(settings_);
    gamepadMapping_ = GamepadMapping::load(settings_);

    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &AppModel::poll);
    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, this, [this] {
        toast_.clear();
        emit toastChanged();
    });

    video_.onVideoSize = [this](int width, int height) {
        QMetaObject::invokeMethod(this, [this, width, height] {
            videoWidth_ = width;
            videoHeight_ = height;
            emit videoSizeChanged();
        }, Qt::QueuedConnection);
    };

    gamepad_.onGimbalRate = [this](float pitch, float yaw) {
        if (session_) session_->setGimbalRate(pitch, yaw);
    };
    gamepad_.onZoom = [this](int direction) { zoom(direction); };
    gamepad_.onAction = [this](GamepadAction action) { handleGamepadAction(action); };
    gamepad_.onLearned = [this](const GamepadLearnTarget& target, const QString& input, bool inverted) {
        gamepadLearned(target, input, inverted);
    };
    gamepad_.onConnectionChange = [this](const QString& name) {
        if (!name.isEmpty() && name != gamepadName_) {
            show(name + QStringLiteral(" connected"), false);
        }
        gamepadName_ = name;
        emit gamepadChanged();
    };
    gamepad_.setMapping(gamepadMapping_);
    gamepad_.setShaper(gimbalTuning_);
    gamepad_.start();
}

AppModel::~AppModel() {
    disconnectFromPayload();
    gamepad_.stop();
    video_.setSink(nullptr);
}

void AppModel::attachWindow(QQuickWindow* window) {
    // Render thread, right after the frame went to the screen.
    connect(window, &QQuickWindow::frameSwapped, this, [this] { video_.framePresented(); },
            Qt::DirectConnection);
}

void AppModel::setVideoSink(QObject* sink) { video_.setSink(qobject_cast<QVideoSink*>(sink)); }

// MARK: Connect / disconnect

void AppModel::connectToPayload() {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                 [this](const ConnectionProfile& p) { return p.id == selectedProfileId_; });
    if (it == profiles_.end()) {
        showConnections_ = true;
        emit uiChanged();
        return;
    }
    const ConnectionProfile profile = *it;
    if (const QString problem = profile.validationError(); !problem.isEmpty()) {
        setConnectError(problem);
        showConnections_ = true;
        emit uiChanged();
        return;
    }
    disconnectFromPayload();
    setConnectError({});
    const int currentGeneration = generation_;
    auto created = PayloadSession::create(profile, &video_, [this, currentGeneration](const PayloadEvent& event) {
        QMetaObject::invokeMethod(this, [this, currentGeneration, event] {
            if (generation_ == currentGeneration) handle(event);
        }, Qt::QueuedConnection);
    });
    if (!created) {
        setConnectError(QStringLiteral("The payload address is not valid."));
        return;
    }
    if (!created->start()) {
        setConnectError(QStringLiteral("Cannot open the MAVLink port. Another app may be using it."));
        return;
    }
    session_ = std::move(created);
    videoMessage_ = profile.videoURL.trimmed().isEmpty() ? QStringLiteral("Waiting for the payload's video address…")
                                                         : QStringLiteral("Connecting video…");
    SystemIntegration::setKeepAwake(true);
    pollTimer_.start();
    emit connectionChanged();
    emit videoStateChanged();
}

void AppModel::disconnectFromPayload() {
    if (!session_) {
        return;
    }
    generation_ += 1;
    session_->setGimbalRate(0.0f, 0.0f);
    if (zoomDirection_ != 0) {
        session_->zoom(0);
        zoomDirection_ = 0;
        emit zoomDirectionChanged();
    }
    std::unique_ptr<PayloadSession> closing = std::move(session_);
    closing->stop(); // joins the core threads: no more frames after this
    closing.reset();
    pollTimer_.stop();
    video_.reset();
    SystemIntegration::setKeepAwake(false);
    resetPayloadState();
    emit connectionChanged();
}

void AppModel::resetPayloadState() {
    videoActive_ = false;
    videoMessage_.clear();
    videoResyncing_ = false;
    sinceResync_.invalidate();
    link_ = {};
    gimbal_ = {};
    camera_ = {};
    tracking_ = {};
    metrics_ = {};
    vehicleHeading_ = -1.0f;
    videoLagMs_ = 0;
    detectionEnabled_ = false;
    cameraValues_.clear();
    gimbalControlSettled_ = false;
    isRecordingLog_ = false; // the core closes the file with the session
    foreignCommandCount_ = 0;
    pendingLabels_.clear();
    videoWidth_ = 0;
    videoHeight_ = 0;
    emit videoStateChanged();
    emit linkChanged();
    emit gimbalChanged();
    emit cameraChanged();
    emit trackingChanged();
    emit metricsChanged();
    emit vehicleHeadingChanged();
    emit videoLagChanged();
    emit videoSizeChanged();
    emit uiChanged();
    emit cameraValuesChanged();
    emit logChanged();
}

void AppModel::setConnectError(const QString& error) {
    if (error != connectError_) {
        connectError_ = error;
        emit connectErrorChanged();
    }
}

// MARK: State polling

void AppModel::poll() {
    if (!session_) {
        return;
    }
    const imago_state s = session_->state();
    if (const auto v = Snapshot::link(s); !(v == link_)) {
        link_ = v;
        emit linkChanged();
    }
    if (const auto v = Snapshot::gimbal(s); !(v == gimbal_)) {
        gimbal_ = v;
        emit gimbalChanged();
    }
    takeGimbalControlOnce();
    if (const auto v = Snapshot::camera(s); !(v == camera_)) {
        camera_ = v;
        emit cameraChanged();
    }
    if (const auto v = Snapshot::tracking(s); !(v == tracking_)) {
        tracking_ = v;
        emit trackingChanged();
    }
    if (const auto v = Snapshot::info(s); !(v == info_)) {
        info_ = v;
        emit infoChanged();
    }
    if (const float v = Snapshot::vehicleHeading(s); v != vehicleHeading_) {
        vehicleHeading_ = v;
        emit vehicleHeadingChanged();
    }
    if (const int v = Snapshot::videoLagSteps(s); v != videoLagMs_) {
        videoLagMs_ = v;
        emit videoLagChanged();
    }
    if (showHud_) {
        VideoMetrics m = Snapshot::metrics(s);
        const VideoDecoder::Counters counters = video_.counters();
        m.decodeFailures = static_cast<int>(counters.decodeFailures);
        m.framesSkipped = static_cast<int>(counters.skipped);
        const QString decoder = video_.decoderName();
        m.decoder = decoder.isEmpty() ? QStringLiteral("—") : decoder;
        if (!(m == metrics_)) {
            metrics_ = m;
            emit metricsChanged();
        }
    }
    gamepad_.zoomLevel = std::max(1.0f, camera_.zoom);
}

// MARK: Events

void AppModel::handle(const PayloadEvent& event) {
    switch (event.type) {
    case IMAGO_EVENT_LINK_UP:
        show(QStringLiteral("Payload connected"), false);
        if (session_ && session_->profile().setStandardRCMode) {
            session_->setCameraParam(QStringLiteral("RC_MODE"), 1);
        }
        // Keep the payload's camera mode in line with the rail.
        if (session_) registerRequest(session_->setCameraMode(captureMode_), QStringLiteral("Camera mode"));
        break;
    case IMAGO_EVENT_LINK_DOWN:
        if (session_) show(QStringLiteral("Payload link lost"), true);
        break;
    case IMAGO_EVENT_VIDEO_STARTED:
        videoActive_ = true;
        videoResyncing_ = false;
        videoMessage_.clear();
        emit videoStateChanged();
        if (event.code == IMAGO_RTSP_TCP && session_ &&
            session_->profile().videoTransport == ConnectionProfile::VideoTransport::Auto) {
            show(QStringLiteral("Video over TCP: UDP did not get through"), false);
        }
        break;
    case IMAGO_EVENT_VIDEO_STOPPED:
        videoActive_ = false;
        emit videoStateChanged();
        break;
    case IMAGO_EVENT_VIDEO_ERROR:
        videoActive_ = false;
        videoResyncing_ = false;
        videoMessage_ = QStringLiteral("Video: ") + event.text;
        emit videoStateChanged();
        break;
    case IMAGO_EVENT_VIDEO_RESYNC:
        videoResyncing_ = true;
        emit videoStateChanged();
        if (sinceResync_.isValid() && sinceResync_.elapsed() < 30'000) {
            // Falling behind again right away: the link cannot carry this stream.
            show(QStringLiteral("Video keeps falling behind. Lower the stream bitrate or resolution."), true);
        } else {
            show(QStringLiteral("Video was %1 s behind live. Catching up…").arg(event.code / 1000.0, 0, 'f', 1), false);
        }
        sinceResync_.start();
        break;
    case IMAGO_EVENT_COMMAND_RESULT:
        finish(event.requestId, event.result, commandName(static_cast<int>(event.code)));
        break;
    case IMAGO_EVENT_PARAM_RESULT:
        finish(event.requestId, event.result, paramName(event.text));
        break;
    case IMAGO_EVENT_STREAM_URI:
        break;
    case IMAGO_EVENT_CAMERA_PARAM:
        cameraValues_.insert(event.text, static_cast<int>(event.code));
        emit cameraValuesChanged();
        break;
    case IMAGO_EVENT_FOREIGN_COMMAND:
        foreignCommandSeen(static_cast<int>(event.code), event.text);
        break;
    case IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY: {
        QString change = event.text;
        show(QStringLiteral("Zoom moved without this app: ") + change.replace(QLatin1Char('x'), QStringLiteral("×")), true);
        break;
    }
    case IMAGO_EVENT_STATUS_TEXT:
    default:
        if (!event.text.isEmpty()) show(event.text, false);
        break;
    }
}

void AppModel::registerRequest(uint32_t requestId, const QString& label) {
    if (requestId != 0) {
        pendingLabels_.insert(requestId, label);
    }
}

void AppModel::finish(uint32_t requestId, int32_t result, const QString& fallback) {
    const QString label = pendingLabels_.take(requestId);
    if (result == IMAGO_RESULT_ACCEPTED) {
        return;
    }
    const QString name = label.isEmpty() ? fallback : label;
    if (result == IMAGO_RESULT_TIMEOUT) {
        show(name + QStringLiteral(": no response from the payload"), true);
    } else {
        show(QStringLiteral("%1 was rejected (code %2)").arg(name).arg(result), true);
    }
}

void AppModel::show(const QString& text, bool error) {
    toast_ = {{QStringLiteral("id"), ++toastCounter_},
              {QStringLiteral("text"), text},
              {QStringLiteral("error"), error}};
    emit toastChanged();
    toastTimer_.start(error ? 4000 : 2500);
}

// MARK: Gimbal

void AppModel::joystickMoved(double x, double y) {
    const GimbalInputShaper::Rates rates =
        gimbalTuning_.rates(static_cast<float>(x), static_cast<float>(y), std::max(1.0f, camera_.zoom));
    if (session_) session_->setGimbalRate(rates.pitch, rates.yaw);
}

void AppModel::setGimbalMode(int mode) {
    if (session_) registerRequest(session_->setGimbalMode(mode), QStringLiteral("Gimbal mode"));
}

bool AppModel::usesGimbalManager() const {
    return session_ && session_->profile().gimbalProtocol == ConnectionProfile::GimbalProtocol::Manager;
}

void AppModel::toggleGimbalControl() {
    const bool take = gimbal_.control != Payload::ControlThisApp;
    if (session_) {
        registerRequest(session_->takeGimbalControl(take),
                        take ? QStringLiteral("Take gimbal control") : QStringLiteral("Release gimbal control"));
    }
}

/// Once per connection, as soon as the gimbal manager answers: take control, unless a ground
/// station holds it (then the rail button decides). Never again after a link blip.
void AppModel::takeGimbalControlOnce() {
    if (!usesGimbalManager() || gimbalControlSettled_ || gimbal_.control == Payload::ControlUnknown) {
        return;
    }
    gimbalControlSettled_ = true;
    if (gimbal_.control == Payload::ControlThisApp) {
        return;
    }
    if (gimbal_.control == Payload::ControlOther &&
        (gimbal_.controlSystem == 255 || gimbal_.controlComponent == 190)) {
        show(gimbal_.controlHolder + QStringLiteral(" holds the gimbal. Click TAKE CONTROL to drive it from here."), false);
        return;
    }
    registerRequest(session_->takeGimbalControl(true), QStringLiteral("Take gimbal control"));
}

void AppModel::recenterGimbal() {
    if (session_) registerRequest(session_->setGimbalMode(Payload::GimbalReset), QStringLiteral("Recenter"));
}

// MARK: Camera

void AppModel::chooseCaptureMode(int mode) {
    if (mode == captureMode_) {
        return;
    }
    captureMode_ = mode;
    emit uiChanged();
    if (session_) registerRequest(session_->setCameraMode(mode), QStringLiteral("Camera mode"));
}

void AppModel::shutter() {
    if (captureMode_ == Payload::VideoMode) {
        toggleRecording();
    } else if (session_) {
        registerRequest(session_->takePhoto(), QStringLiteral("Photo"));
    }
}

void AppModel::toggleRecording() {
    if (captureMode_ != Payload::VideoMode) {
        captureMode_ = Payload::VideoMode;
        emit uiChanged();
    }
    const bool start = !camera_.recording;
    if (session_) {
        registerRequest(session_->setRecording(start),
                        start ? QStringLiteral("Start recording") : QStringLiteral("Stop recording"));
    }
}

void AppModel::zoom(int direction) {
    if (direction != zoomDirection_) {
        zoomDirection_ = direction;
        emit zoomDirectionChanged();
    }
    if (!session_) {
        return;
    }
    const uint32_t id = session_->zoom(direction);
    if (direction != 0) registerRequest(id, QStringLiteral("Zoom"));
}

void AppModel::zoomTo(double level) {
    if (session_) session_->zoomTarget(static_cast<float>(level));
}

QVariantList AppModel::zoomLevels(bool superResolution) const {
    QVariantList result;
    for (const float level : PayloadSession::zoomLevels(superResolution)) {
        result.append(level);
    }
    return result;
}

void AppModel::setView(int source) {
    if (session_) {
        registerRequest(session_->setView(source), QStringLiteral("View ") + Payload::viewSourceTitle(source));
    }
}

void AppModel::cycleView(int step) {
    const QList<int> all = Payload::viewSources();
    const qsizetype index = all.indexOf(camera_.view);
    const qsizetype current = index < 0 ? 0 : index;
    setView(all[(current + step + all.size()) % all.size()]);
}

// MARK: Tracking

void AppModel::videoTapped(double x, double y) {
    if (!session_) {
        return;
    }
    if (pointMode_) {
        registerRequest(session_->pointGimbal(static_cast<float>(x), static_cast<float>(y)), QStringLiteral("Point gimbal"));
        return;
    }
    if (const Detection* detection = tracking_.hit(x, y)) {
        // The detection's own box: the tracker starts on the whole object the AI found.
        registerRequest(session_->track(detection->rect), QStringLiteral("Track ") + detection->label());
    } else {
        registerRequest(session_->track(static_cast<float>(x), static_cast<float>(y)), QStringLiteral("Track"));
    }
}

void AppModel::stopTracking() {
    if (session_) registerRequest(session_->stopTracking(), QStringLiteral("Stop tracking"));
}

void AppModel::toggleDetection() {
    detectionEnabled_ = !detectionEnabled_;
    emit uiChanged();
    if (session_) registerRequest(session_->setDetection(detectionEnabled_), QStringLiteral("AI detection"));
}

// MARK: Gamepad

void AppModel::handleGamepadAction(GamepadAction action) {
    switch (action) {
    case GamepadAction::Shutter:
        shutter();
        break;
    case GamepadAction::Record:
        toggleRecording();
        break;
    case GamepadAction::StopTracking:
        stopTracking();
        break;
    case GamepadAction::ToggleDetection:
        toggleDetection();
        break;
    case GamepadAction::PreviousView:
        cycleView(-1);
        break;
    case GamepadAction::NextView:
        cycleView(1);
        break;
    case GamepadAction::Recenter:
        recenterGimbal();
        break;
    case GamepadAction::TogglePointMode:
        pointMode_ = !pointMode_;
        emit uiChanged();
        break;
    case GamepadAction::ToggleLockFollow:
        setGimbalMode(gimbal_.mode == Payload::GimbalLock ? Payload::GimbalFollow : Payload::GimbalLock);
        break;
    case GamepadAction::ZoomIn:
    case GamepadAction::ZoomOut:
        break; // held: sent through onZoom
    }
}

QVariantList AppModel::gamepadAxes() const {
    QVariantList rows;
    for (const GamepadAxisFunction function : GamepadMapping::allFunctions()) {
        const auto it = gamepadMapping_.axes.constFind(function);
        const bool bound = it != gamepadMapping_.axes.cend();
        rows.append(QVariantMap{
            {QStringLiteral("key"), GamepadMapping::key(function)},
            {QStringLiteral("title"), GamepadMapping::title(function)},
            {QStringLiteral("bound"), bound},
            {QStringLiteral("label"), bound ? GamepadInput::label(it->input) : QString()},
            {QStringLiteral("inverted"), bound && it->inverted},
        });
    }
    return rows;
}

QVariantList AppModel::gamepadButtons() const {
    QVariantList rows;
    for (const GamepadAction action : GamepadMapping::allActions()) {
        const QString input = gamepadMapping_.button(action);
        rows.append(QVariantMap{
            {QStringLiteral("key"), GamepadMapping::key(action)},
            {QStringLiteral("title"), GamepadMapping::title(action)},
            {QStringLiteral("bound"), !input.isEmpty()},
            {QStringLiteral("label"), input.isEmpty() ? QString() : GamepadInput::label(input)},
        });
    }
    return rows;
}

void AppModel::learnGamepadInput(const QString& target) {
    gamepadLearning_ = GamepadLearnTarget::fromKey(target);
    gamepad_.setLearning(gamepadLearning_);
    emit gamepadMappingChanged();
}

void AppModel::gamepadLearned(const GamepadLearnTarget& target, const QString& input, bool inverted) {
    // Cancelled, or another assignment started, while this input was on its way.
    if (!gamepadLearning_ || !(*gamepadLearning_ == target)) {
        return;
    }
    gamepadLearning_.reset();
    if (target.kind == GamepadLearnTarget::Kind::Action) {
        updateGamepadMapping(gamepadMapping_.bound(target.action, input));
    } else {
        updateGamepadMapping(gamepadMapping_.bound(target.function, input, inverted));
    }
}

void AppModel::clearGamepadAxis(const QString& function) {
    GamepadAxisFunction f;
    if (GamepadMapping::functionFromKey(function, &f)) updateGamepadMapping(gamepadMapping_.unbound(f));
}

void AppModel::clearGamepadButton(const QString& action) {
    GamepadAction a;
    if (GamepadMapping::actionFromKey(action, &a)) updateGamepadMapping(gamepadMapping_.unbound(a));
}

void AppModel::setGamepadAxisInverted(const QString& function, bool inverted) {
    GamepadAxisFunction f;
    if (GamepadMapping::functionFromKey(function, &f)) updateGamepadMapping(gamepadMapping_.withInverted(f, inverted));
}

void AppModel::resetGamepadMapping() {
    gamepadLearning_.reset();
    gamepad_.setLearning(std::nullopt);
    updateGamepadMapping(GamepadMapping::standard());
}

void AppModel::updateGamepadMapping(const GamepadMapping& mapping) {
    gamepadMapping_ = mapping;
    mapping.save(settings_);
    gamepad_.setMapping(mapping);
    emit gamepadMappingChanged();
}

// MARK: Profiles

QVariantList AppModel::profiles() const {
    QVariantList list;
    for (const ConnectionProfile& profile : profiles_) {
        list.append(profile.toVariant());
    }
    return list;
}

QVariantMap AppModel::selectedProfile() const {
    for (const ConnectionProfile& profile : profiles_) {
        if (profile.id == selectedProfileId_) return profile.toVariant();
    }
    return {};
}

QVariantMap AppModel::connectedProfile() const { return session_ ? session_->profile().toVariant() : QVariantMap(); }

void AppModel::selectProfile(const QString& id) {
    selectedProfileId_ = id;
    store_.setSelectedId(id);
    emit profilesChanged();
}

QVariantMap AppModel::newProfile() const { return ConnectionProfile::make(QStringLiteral("New payload")).toVariant(); }

QString AppModel::validateProfile(const QVariantMap& profile) const {
    return ConnectionProfile::fromVariant(profile).validationError();
}

void AppModel::saveProfile(const QVariantMap& map) {
    ConnectionProfile profile = ConnectionProfile::fromVariant(map);
    profile.name = profile.name.trimmed();
    profile.payloadIP = profile.payloadIP.trimmed();
    profile.videoURL = profile.videoURL.trimmed();
    const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&profile](const ConnectionProfile& p) { return p.id == profile.id; });
    if (it != profiles_.end()) {
        *it = profile;
    } else {
        profiles_.append(profile);
    }
    store_.save(profiles_);
    if (selectedProfileId_.isEmpty()) {
        selectProfile(profile.id);
    }
    emit profilesChanged();
}

void AppModel::deleteProfile(const QString& id) {
    profiles_.removeIf([&id](const ConnectionProfile& p) { return p.id == id; });
    store_.save(profiles_);
    if (selectedProfileId_ == id) {
        selectProfile(profiles_.isEmpty() ? QString() : profiles_.first().id);
    }
    emit profilesChanged();
}

// MARK: Settings

void AppModel::toggleAttitude() {
    showAttitude_ = !showAttitude_;
    settings_.setValue(QLatin1String(kShowAttitudeKey), showAttitude_);
    emit uiChanged();
}

void AppModel::readSettings(int page, bool onlyMissing) {
    if (!session_) {
        return;
    }
    for (const CameraSettingSection& section : CameraSettingsCatalog::sections(page)) {
        for (const CameraSetting& setting : section.settings) {
            if (!onlyMissing || !cameraValues_.contains(setting.id)) {
                session_->readCameraParam(setting.id);
            }
        }
    }
}

QVariantList AppModel::settingsSections(int page) const { return CameraSettingsCatalog::sectionsVariant(page); }

void AppModel::setCameraSetting(const QString& id, const QString& title, int value) {
    if (session_) registerRequest(session_->setCameraParam(id, value), title);
}

void AppModel::setStreamBitrate(int bitsPerSecond) {
    if (session_) registerRequest(session_->setStreamBitrate(0, bitsPerSecond), QStringLiteral("Stream bitrate"));
}

void AppModel::setGimbalTuning(const QString& field, double value) {
    GimbalInputShaper tuning = gimbalTuning_;
    if (tuning.set(field, value) && !(tuning == gimbalTuning_)) {
        updateGimbalTuning(tuning);
    }
}

void AppModel::resetGimbalTuning() { updateGimbalTuning(GimbalInputShaper()); }

void AppModel::updateGimbalTuning(const GimbalInputShaper& tuning) {
    gimbalTuning_ = tuning;
    tuning.save(settings_);
    gamepad_.setShaper(tuning);
    emit gimbalTuningChanged();
}

// MARK: Diagnostics

QString AppModel::logsDirectory() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/Logs");
}

void AppModel::toggleLogRecording() {
    if (!session_) {
        return;
    }
    if (isRecordingLog_) {
        const bool complete = session_->stopRecording();
        isRecordingLog_ = false;
        emit logChanged();
        show(complete ? QStringLiteral("MAVLink log saved") : QStringLiteral("MAVLink log saved, but incomplete (disk full?)"),
             !complete);
        return;
    }
    const QString directory = logsDirectory();
    if (!QDir().mkpath(directory)) {
        show(QStringLiteral("Cannot create the log folder"), true);
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd'T'HHmmss"));
    QString path = QStringLiteral("%1/imago-%2.tlog").arg(directory, stamp);
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QStringLiteral("%1/imago-%2-%3.tlog").arg(directory, stamp).arg(n);
    }
    if (!session_->startRecording(path)) {
        show(QStringLiteral("Cannot write the MAVLink log"), true);
        return;
    }
    isRecordingLog_ = true;
    lastLogFile_ = path;
    emit logChanged();
}

void AppModel::showLogFolder() {
    const QString directory = logsDirectory();
    QDir().mkpath(directory);
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

/// The payload acknowledged a command that came from elsewhere (autopilot, GCS).
void AppModel::foreignCommandSeen(int command, const QString& from) {
    foreignCommandCount_ += 1;
    emit logChanged();
    if (!isCameraCommand(command)) {
        return;
    }
    // At most one warning every few seconds: a GCS can repeat a command.
    if (sinceForeignToast_.isValid() && sinceForeignToast_.elapsed() < 3000) {
        return;
    }
    sinceForeignToast_.start();
    show(QStringLiteral("%1 sent to the camera by %2").arg(commandName(command), senderName(from)), true);
}

// MARK: Fixed lists

QVariantList AppModel::viewSources() const {
    QVariantList list;
    for (const int source : Payload::viewSources()) {
        list.append(option(source, Payload::viewSourceTitle(source)));
    }
    return list;
}

QVariantList AppModel::gimbalModes() const {
    QVariantList list;
    for (const int mode : {Payload::GimbalLock, Payload::GimbalFollow, Payload::GimbalMapping, Payload::GimbalOff}) {
        list.append(option(mode, Payload::gimbalModeTitle(mode)));
    }
    return list;
}

QStringList AppModel::settingsPages() const { return CameraSettingsCatalog::pageTitles(); }

QVariantList AppModel::bitrates() const {
    QVariantList list;
    for (const int bps : CameraSettingsCatalog::bitrates()) {
        list.append(option(bps, QStringLiteral("%1 Mb/s").arg(bps / 1'000'000)));
    }
    return list;
}

QVariantList AppModel::videoLagChoices() const {
    QVariantList list;
    for (const int ms : ConnectionProfile::videoLagChoices) {
        list.append(option(ms, ms == 0 ? QStringLiteral("Never")
                               : ms < 1000 ? QStringLiteral("%1 ms").arg(ms)
                                           : QStringLiteral("%1 s").arg(ms / 1000)));
    }
    return list;
}

QVariantList AppModel::smoothingChoices() const {
    QVariantList list;
    for (const int ms : ConnectionProfile::smoothingChoices) {
        list.append(option(ms, ms == 0 ? QStringLiteral("Off") : QStringLiteral("%1 ms").arg(ms)));
    }
    return list;
}

QString AppModel::version() const { return QStringLiteral(IMAGO_APP_VERSION); }
