// The thermal bar (IR palette, zoom, gain, the camera's isotherm) and the payload's web settings
// page, whose isotherm the bar uses on a camera without one of its own (our Orus L).

#include "app/AppModel.h"

#include <algorithm>

// MARK: Thermal bar

/// On every poll: once the view turns IR, asks for the settings the bar shows, and follows the
/// changes the data stream reports, so a palette changed elsewhere shows here too.
void AppModel::updateThermal(const ThermalStatus& status) {
    const bool statusChanged = !(status == thermal_);
    if (statusChanged) {
        const ThermalStatus previous = thermal_;
        thermal_ = status;
        if (Thermal::followStream(cameraValues_, previous.palette, status.palette, previous.zoom, status.zoom)) {
            emit cameraValuesChanged();
        }
    }
    const bool irView = camera_.view == IMAGO_VIEW_IR || camera_.view == IMAGO_VIEW_IR_EO;
    if (irView && !thermalSettingsRead_ && session_) {
        thermalSettingsRead_ = true;
        for (const QString& id : Thermal::settingsToRead()) {
            session_->readCameraParam(id);
        }
    } else if (!irView) {
        thermalSettingsRead_ = false;
    }
    // Our Orus L has its isotherm on its web page only: the bar reads that page itself, and again
    // every 10 s until it answers (it may come up after MAVLink does). Before, ISO stayed hidden
    // on this payload and the isotherm was in Settings alone (iOS, 2026-11-19).
    const qint64 now = clock_.elapsed();
    if (irView && session_ && link_.up && !irSettings_.isothermSupported && !webIsotherm_ &&
        !webRequests_.values().contains(WebRequest::AutoRead) && now >= nextWebIsothermReadMs_) {
        nextWebIsothermReadMs_ = now + Thermal::kWebReadRetryMs;
        startWebRequest(session_->readWebIRSettings(), WebRequest::AutoRead);
    }
    refreshThermal(statusChanged);
}

void AppModel::refreshThermal(bool statusChanged) {
    const bool shows = session_ && (camera_.view == IMAGO_VIEW_IR || camera_.view == IMAGO_VIEW_IR_EO);
    const Thermal::Settings ir = Thermal::settings(cameraValues_, thermal_.palette, thermal_.zoom, isothermBand_);
    if (statusChanged || shows != showsThermalBar_ || !(ir == irSettings_)) {
        showsThermalBar_ = shows;
        irSettings_ = ir;
        emit thermalChanged();
    }
}

QVariantList AppModel::irPalettes() const {
    QVariantList list;
    for (int i = 0; i < IMAGO_IR_PALETTE_COUNT; ++i) {
        list.append(QVariantMap{{QStringLiteral("value"), i},
                                {QStringLiteral("title"), Thermal::paletteName(thermal_.cameraType, i)}});
    }
    return list;
}

QStringList AppModel::irPaletteColors() const {
    return Thermal::paletteColors(thermal_.cameraType, std::max(irSettings_.palette, 0));
}

QVariantMap AppModel::thermalScale() const {
    const bool valid = thermal_.hasTemperatures;
    const auto mean = valid ? Thermal::fraction(thermal_.meanC, thermal_.minC, thermal_.maxC) : std::nullopt;
    const std::optional<int> webThreshold =
        webIsotherm_ && webIsotherm_->isotherm_enabled ? std::optional<int>(webIsotherm_->isotherm_threshold_c)
                                                       : std::nullopt;
    const auto outlined = Thermal::scaleBand(irSettings_, webThreshold);
    const auto band = valid && outlined ? Thermal::bandSpan(*outlined, thermal_.minC, thermal_.maxC) : std::nullopt;
    return {
        {QStringLiteral("valid"), valid},
        {QStringLiteral("minText"), valid ? Thermal::celsius(thermal_.minC) : QStringLiteral("—")},
        {QStringLiteral("maxText"), valid ? Thermal::celsius(thermal_.maxC) : QStringLiteral("—")},
        {QStringLiteral("meanText"), valid ? Thermal::celsius(thermal_.meanC) : QString()},
        {QStringLiteral("meanAt"), mean.value_or(-1.0)},
        {QStringLiteral("bandFrom"), band ? band->from : -1.0},
        {QStringLiteral("bandTo"), band ? band->to : -1.0},
    };
}

QVariantList AppModel::isothermPresets() const {
    QVariantList list;
    for (const Thermal::Preset& preset : Thermal::presets()) {
        list.append(QVariantMap{{QStringLiteral("name"), preset.name},
                                {QStringLiteral("low"), preset.band.low},
                                {QStringLiteral("high"), preset.band.high}});
    }
    return list;
}

void AppModel::setIRPalette(int index) {
    if (session_) registerRequest(session_->setCameraParam(QStringLiteral("C_T_PALETTE"), index), QStringLiteral("Palette"));
}

void AppModel::stepIRZoom(int step) { setIRZoom((irSettings_.zoom > 0 ? irSettings_.zoom : 1) + step); }

void AppModel::setIRZoom(int zoom) {
    if (!session_) {
        return;
    }
    const int next = std::clamp(zoom, Thermal::kMinZoom, Thermal::kMaxZoom);
    if (next == irSettings_.zoom) {
        return;
    }
    registerRequest(session_->setCameraParam(QStringLiteral("C_T_ZOOM"), next - 1), QStringLiteral("IR zoom"));
}

void AppModel::setIRLowGain(bool low) {
    if (session_) {
        registerRequest(session_->setCameraParam(QStringLiteral("C_T_G"), low ? IMAGO_IR_GAIN_LOW : IMAGO_IR_GAIN_HIGH),
                        QStringLiteral("IR gain"));
    }
}

void AppModel::applyIsotherm(int low, int high) {
    const Thermal::Band band{low, high};
    if (!session_ || !Thermal::isValid(band)) {
        return;
    }
    isothermBand_ = band;
    Thermal::rememberBand(settings_, band);
    registerRequest(session_->setIsotherm(true, low, high), QStringLiteral("Isotherm"));
    refreshThermal(false);
}

void AppModel::disableIsotherm() {
    if (session_) registerRequest(session_->setIsotherm(false), QStringLiteral("Isotherm off"));
}

// MARK: Web settings

QVariantMap AppModel::webIsotherm() const {
    if (!webIsotherm_) {
        return {};
    }
    return {{QStringLiteral("enabled"), webIsotherm_->isotherm_enabled},
            {QStringLiteral("threshold"), webIsotherm_->isotherm_threshold_c},
            {QStringLiteral("range"), webIsotherm_->measurement_range}};
}

QVariantList AppModel::webIsothermPresets() const {
    QVariantList list;
    for (const Thermal::WebPreset& preset : Thermal::webPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), preset.name}, {QStringLiteral("threshold"), preset.thresholdC}});
    }
    return list;
}

void AppModel::readWebIsotherm() {
    if (session_ && !webBusy()) startWebRequest(session_->readWebIRSettings(), WebRequest::Read);
}

/// Not held back by a read on its way (the thermal bar's own may be): the core sends web
/// requests one at a time.
void AppModel::saveWebIsotherm(bool enabled, int thresholdC, int measurementRange) {
    if (session_) {
        startWebRequest(session_->setWebIsotherm(enabled, thresholdC, measurementRange), WebRequest::Save);
    }
}

/// Over 150 °C the core picks the high measurement range, which alone reaches it.
void AppModel::setWebIsotherm(bool on, int thresholdC) {
    if (!webIsotherm_) {
        return;
    }
    const int threshold = Thermal::clampWebThreshold(thresholdC >= 0 ? thresholdC : webIsotherm_->isotherm_threshold_c);
    saveWebIsotherm(on, threshold, imago_web_isotherm_range_for(threshold, webIsotherm_->measurement_range));
}

void AppModel::restartPayloadService() {
    if (session_ && !webBusy()) startWebRequest(session_->restartPayloadService(), WebRequest::RestartService);
}

void AppModel::startWebRequest(uint32_t requestId, WebRequest kind) {
    if (requestId == 0) {
        if (kind == WebRequest::AutoRead) {
            setWebIsothermFailed(true);
            warnWebIsothermOnce(QStringLiteral("Isotherm: needs the payload IP in the connection profile (its web page)"));
        } else {
            show(QStringLiteral("The payload's web page needs the payload IP in the connection profile"), true);
        }
        return;
    }
    webRequests_.insert(requestId, kind);
    emit webChanged();
}

void AppModel::webRequestEnded(uint32_t requestId, uint32_t status, int32_t result, const QString& error) {
    const auto it = webRequests_.constFind(requestId);
    if (it == webRequests_.cend()) {
        return;
    }
    const WebRequest kind = *it;
    webRequests_.erase(it);
    if (result == IMAGO_RESULT_ACCEPTED && kind != WebRequest::RestartService && session_) {
        if (const imago_web_ir_settings settings = session_->webIRSettings(); settings.valid) {
            webIsotherm_ = settings;
            emit thermalChanged(); // the scale outlines the page's isotherm
        }
    }
    emit webChanged();
    if (kind == WebRequest::AutoRead) {
        setWebIsothermFailed(result != IMAGO_RESULT_ACCEPTED);
        if (webIsothermFailed_) {
            warnWebIsothermOnce(QStringLiteral("Isotherm: the payload's web page does not answer (payload IP, port 8000)"));
        }
    } else if (kind == WebRequest::Save && result == IMAGO_RESULT_ACCEPTED) {
        show(QStringLiteral("Isotherm saved"), false);
    } else if (kind == WebRequest::RestartService &&
               (result == IMAGO_RESULT_ACCEPTED || result == IMAGO_RESULT_TIMEOUT)) {
        // The service may go down before the page answers.
        show(QStringLiteral("Camera service restarting. Video and control come back by themselves."), false);
    } else if (!(kind == WebRequest::Read && result == IMAGO_RESULT_ACCEPTED)) {
        show(PayloadMessages::webFailed(kind, status, error), true);
    }
}

void AppModel::setWebIsothermFailed(bool failed) {
    if (failed != webIsothermFailed_) {
        webIsothermFailed_ = failed;
        emit webChanged();
    }
}

/// The thermal bar's own read says so once per connection; the ISO button stays grey.
void AppModel::warnWebIsothermOnce(const QString& text) {
    if (!webIsothermWarned_) {
        webIsothermWarned_ = true;
        show(text, true);
    }
}
