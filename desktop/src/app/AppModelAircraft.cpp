// The aircraft's figures, the radio link, the map, the AI button's wait for boxes, and the payload
// settings that are shown as asked because it does not report them back (range finder rate, its
// own stream).

#include "app/AppModel.h"

#include "core/CameraSettings.h"
#include "core/StreamSettings.h"

#include <QTimer>

void AppModel::pollAircraft(const imago_state& s) {
    if (const QList<Aircraft::Figure> v = Aircraft::figures(s); v != aircraft_) {
        aircraft_ = v;
        emit aircraftChanged();
    }
    map_.setAircraft(s);
    detectionFrame_ = s.detection_frame;
    // AI detection was asked for and no box has come: through Herelink with PX4 forwarding they
    // never did (flight 2026-11-13), so say it once instead of leaving the operator pressing the
    // button again.
    if (aiButton_.update(clock_.elapsed(), s.link_up, s.detection_frame, s.detection_count)) {
        show(QStringLiteral("AI detection is on, but no boxes reach the app on this link"), true);
    }
}

// MARK: Radio link

void AppModel::pollRadio(const imago_state& s) {
    if (const RadioStatus v = Snapshot::radio(s); !(v == radio_)) {
        radio_ = v;
        emit radioChanged();
    }
    // Once when it turns critical or is lost; the chip in the top bar keeps showing it.
    if (const auto warning = radioWarning_.update(radio_.level, radio_.valid, radio_.rssi, radio_.snr)) {
        show(*warning, true);
    }
}

// MARK: Range finder

void AppModel::setRangeFinderMode(int mode) {
    if (!session_) {
        return;
    }
    const uint32_t id = session_->setRangeFinderMode(mode);
    if (id == 0) {
        return;
    }
    registerRequest(id, QStringLiteral("Range finder"));
    // Shown as asked: through Herelink the acknowledgement often never comes though the setting
    // applied (2026-11-13). A read a moment later corrects it if not.
    cameraValues_.insert(CameraSettingsCatalog::kRangeFinderMode, mode);
    emit cameraValuesChanged();
    const int currentGeneration = generation_;
    QTimer::singleShot(StreamSettings::kRangeFinderReadBackMs, this, [this, currentGeneration] {
        if (session_ && generation_ == currentGeneration) {
            session_->readCameraParam(CameraSettingsCatalog::kRangeFinderMode);
        }
    });
}

// MARK: The Orus L's own stream

QVariantList AppModel::streamCameras() const {
    QVariantList list;
    for (const int camera : StreamSettings::cameras()) {
        list.append(QVariantMap{{QStringLiteral("value"), camera},
                                {QStringLiteral("title"), StreamSettings::cameraTitle(camera)}});
    }
    return list;
}

QVariantList AppModel::streamSizes(int camera) const {
    QVariantList list;
    const QStringList titles = StreamSettings::sizeTitles(camera);
    for (int level = 0; level < titles.size(); ++level) {
        list.append(QVariantMap{{QStringLiteral("value"), level}, {QStringLiteral("title"), titles[level]}});
    }
    return list;
}

void AppModel::setStreamBitrate(int camera, int bitsPerSecond) {
    if (!session_) {
        return;
    }
    const uint32_t id = session_->setStreamBitrate(camera, bitsPerSecond);
    registerRequest(id, QStringLiteral("Stream bitrate"));
    if (id != 0) {
        rememberStreamAsked(camera, QStringLiteral("bitrate"), bitsPerSecond);
    }
}

void AppModel::setStreamResolution(int camera, int level) {
    if (!session_) {
        return;
    }
    const uint32_t id = session_->setStreamResolution(camera, level);
    registerRequest(id, QStringLiteral("Stream resolution"));
    if (id != 0) {
        rememberStreamAsked(camera, QStringLiteral("level"), level);
    }
}

/// The payload reports neither setting back: what Settings shows is what was asked here.
void AppModel::rememberStreamAsked(int camera, const QString& field, int value) {
    const QString key = QString::number(camera);
    QVariantMap asked = streamAsked_.value(key).toMap();
    asked.insert(field, value);
    streamAsked_.insert(key, asked);
    emit streamAskedChanged();
}
