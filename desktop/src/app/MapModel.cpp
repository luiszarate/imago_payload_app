#include "app/MapModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <tuple>

namespace {

constexpr int kConnectTimeoutMs = 8000;

QString megabytes(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1); }

/// "19.4326" for the area fields: enough for a few metres, no trailing noise.
QString degrees(double value) { return QString::number(value, 'f', 5); }

std::optional<double> parse(const QString& text) {
    bool ok = false;
    // Typed with a point or the locale's decimal separator.
    double value = text.trimmed().toDouble(&ok);
    if (!ok) {
        value = QLocale().toDouble(text.trimmed(), &ok);
    }
    return ok && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

} // namespace

MapModel::MapModel(QSettings& settings, const QString& appVersion, QObject* parent)
    : QObject(parent),
      store_(settings),
      settings_(MapSettings::load(settings)),
      userAgent_(QStringLiteral("ImagoPayloadControl/%1 (offline map cache)").arg(appVersion)) {
    clock_.start();
    pool_.setMaxThreadCount(1);
    network_.setTransferTimeout(kConnectTimeoutMs);
}

MapModel::~MapModel() {
    rememberPosition();
    pool_.clear();
    pool_.waitForDone();
}

QString MapModel::root() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tiles");
}

QString MapModel::filePath(const QString& style, const TileId& id) const {
    return root() + QLatin1Char('/') + TileMath::relativePath(style, id);
}

bool MapModel::isStored(const QString& style, const TileId& id) const {
    const QFileInfo info(filePath(style, id));
    return info.isFile() && info.size() > 0;
}

// MARK: Settings

void MapModel::saveSettings() {
    settings_.save(store_);
    emit settingsChanged();
}

void MapModel::setVisible(bool on) {
    if (on != settings_.visible) {
        settings_.visible = on;
        saveSettings();
    }
}

void MapModel::setDownloadDetail(bool on) {
    if (on != settings_.downloadDetail) {
        settings_.downloadDetail = on;
        saveSettings();
    }
}

void MapModel::toggleStyle() {
    settings_.style = settings_.satellite() ? MapSettings::kStreet : MapSettings::kSatellite;
    saveSettings();
    emit revisionChanged(); // other tiles
}

void MapModel::zoomBy(int step) {
    const int next = std::clamp(settings_.zoom + step, TileMath::kMinZoom, TileMath::kMaxZoom);
    if (next == settings_.zoom) {
        return;
    }
    settings_.zoom = next;
    saveSettings();
    emit viewChanged();
}

QVariantMap MapModel::sources() const {
    return {{QStringLiteral("streetUrl"), settings_.streetUrl},
            {QStringLiteral("streetCredit"), settings_.streetCredit},
            {QStringLiteral("satelliteUrl"), settings_.satelliteUrl},
            {QStringLiteral("satelliteCredit"), settings_.satelliteCredit}};
}

QString MapModel::setSources(const QString& streetUrl, const QString& streetCredit, const QString& satelliteUrl,
                             const QString& satelliteCredit) {
    const MapSettings defaults;
    const auto orDefault = [](const QString& value, const QString& fallback) {
        return value.trimmed().isEmpty() ? fallback : value.trimmed();
    };
    // Without {z}, {x} and {y} every tile is one request, stored under every tile's path.
    for (const auto& [url, fallback, name] :
         {std::tuple{satelliteUrl, defaults.satelliteUrl, QStringLiteral("Satellite")},
          std::tuple{streetUrl, defaults.streetUrl, QStringLiteral("Street map")}}) {
        if (const QString problem = TileMath::templateProblem(orDefault(url, fallback)); !problem.isEmpty()) {
            return name + QStringLiteral(": ") + problem;
        }
    }
    settings_.streetUrl = orDefault(streetUrl, defaults.streetUrl);
    settings_.streetCredit = orDefault(streetCredit, defaults.streetCredit);
    settings_.satelliteUrl = orDefault(satelliteUrl, defaults.satelliteUrl);
    settings_.satelliteCredit = orDefault(satelliteCredit, defaults.satelliteCredit);
    missing_.clear(); // a new server may have them
    offlineUntilMs_ = 0;
    saveSettings();
    return {};
}

void MapModel::restoreSources() {
    const MapSettings defaults;
    setSources(defaults.streetUrl, defaults.streetCredit, defaults.satelliteUrl, defaults.satelliteCredit);
}

// MARK: Aircraft

void MapModel::setAircraft(const imago_state& s) {
    const bool changed = s.vehicle_position_valid != hasPosition_ ||
                         (s.vehicle_position_valid && (s.vehicle_lat_deg != lat_ || s.vehicle_lon_deg != lon_)) ||
                         s.target_valid != hasTarget_ ||
                         (s.target_valid && (s.target_lat_deg != targetLat_ || s.target_lon_deg != targetLon_));
    if (!changed) {
        return;
    }
    const bool appeared = s.vehicle_position_valid != hasPosition_;
    hasPosition_ = s.vehicle_position_valid;
    if (hasPosition_) {
        lat_ = s.vehicle_lat_deg;
        lon_ = s.vehicle_lon_deg;
        // Where the aircraft was last seen: the area Settings › Map offers to download.
        settings_.lastLat = lat_;
        settings_.lastLon = lon_;
    }
    hasTarget_ = s.target_valid;
    if (hasTarget_) {
        targetLat_ = s.target_lat_deg;
        targetLon_ = s.target_lon_deg;
    }
    emit viewChanged();
    if (appeared) {
        emit positionChanged();
    }
}

void MapModel::clearAircraft() {
    if (!hasPosition_ && !hasTarget_) {
        return;
    }
    const bool hadPosition = hasPosition_;
    hasPosition_ = false;
    hasTarget_ = false;
    emit viewChanged();
    if (hadPosition) {
        emit positionChanged();
    }
}

void MapModel::rememberPosition() {
    const MapSettings saved = MapSettings::load(store_);
    if (saved.lastLat != settings_.lastLat || saved.lastLon != settings_.lastLon) {
        settings_.save(store_);
    }
}

// MARK: Tiles in flight

QString MapModel::tileSource(int column, int row) {
    const int z = settings_.zoom;
    if (row < 0 || row >= (1 << z)) {
        return {};
    }
    const TileId id{z, TileMath::wrapColumn(column, z), row};
    const QString style = settings_.style;
    const QString path = filePath(style, id);
    if (QFileInfo(path).size() > 0) {
        return QUrl::fromLocalFile(path).toString();
    }
    fetchInFlight(style, id);
    return {};
}

void MapModel::fetchInFlight(const QString& style, const TileId& id) {
    const qint64 now = clock_.elapsed();
    const QString key = TileMath::relativePath(style, id);
    if (now < offlineUntilMs_ || pending_.contains(key) || pending_.size() >= kMaxFlightFetches ||
        now < missing_.value(key, 0) || !TileMath::templateProblem(settings_.url(style)).isEmpty()) {
        return;
    }
    pending_.insert(key);
    QNetworkReply* reply = get(style, id);
    connect(reply, &QNetworkReply::finished, this, [this, reply, style, id, key] {
        reply->deleteLater();
        pending_.remove(key);
        if (store(reply, style, id)) {
            missing_.remove(key);
            revision_ += 1;
            emit revisionChanged();
        } else {
            const qint64 later = clock_.elapsed();
            missing_.insert(key, later + kRetryMs);
            offlineUntilMs_ = later + kOfflineMs;
        }
    });
}

QNetworkReply* MapModel::get(const QString& style, const TileId& id) {
    QNetworkRequest request(QUrl(TileMath::url(settings_.url(style), id)));
    // Tile servers ask for a client name that says who is asking (OpenStreetMap's tile policy).
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network_.get(request);
    // Nothing that large is a tile: stop reading rather than hold it all in memory.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > TileMath::kMaxTileBytes || total > TileMath::kMaxTileBytes) {
            reply->abort();
        }
    });
    return reply;
}

bool MapModel::store(QNetworkReply* reply, const QString& style, const TileId& id) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        return false;
    }
    if (reply->bytesAvailable() > TileMath::kMaxTileBytes) {
        return false;
    }
    const QByteArray bytes = reply->readAll();
    if (!TileMath::isTileImage(bytes)) {
        return false;
    }
    const QString path = filePath(style, id);
    if (!QDir().mkpath(QFileInfo(path).path())) {
        return false;
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

// MARK: Offline area

QVariantMap MapModel::area() const {
    const bool known = settings_.lastLat != 0.0 || settings_.lastLon != 0.0;
    return {{QStringLiteral("lat"), known ? degrees(settings_.lastLat) : QString()},
            {QStringLiteral("lon"), known ? degrees(settings_.lastLon) : QString()},
            {QStringLiteral("radiusKm"), QString::number(settings_.downloadRadiusKm)},
            {QStringLiteral("known"), known}};
}

QString MapModel::download(const QString& style, const QString& lat, const QString& lon, const QString& radiusKm) {
    if (download_.active) {
        return {};
    }
    const auto latitude = parse(lat);
    const auto longitude = parse(lon);
    if (!latitude || !longitude || std::abs(*latitude) > 85.0 || std::abs(*longitude) > 180.0) {
        return QStringLiteral("Type the latitude and longitude of the flight area, in degrees (19.4326, -99.1332).");
    }
    const QString chosen = style == MapSettings::kStreet ? MapSettings::kStreet : MapSettings::kSatellite;
    if (const QString problem = TileMath::templateProblem(settings_.url(chosen)); !problem.isEmpty()) {
        return problem + QStringLiteral(" Fix it in Tile sources.");
    }
    const double km = std::clamp(parse(radiusKm).value_or(3.0), 0.5, 15.0);
    settings_.lastLat = *latitude;
    settings_.lastLon = *longitude;
    settings_.downloadRadiusKm = km;
    saveSettings();

    const TileMath::DownloadPlan plan = TileMath::downloadPlan(*latitude, *longitude, km, settings_.downloadDetail);
    download_ = {};
    download_.active = true;
    download_.style = chosen;
    download_.tiles = plan.tiles;
    download_.topZoom = plan.topZoom;
    download_.lat = *latitude;
    download_.lon = *longitude;
    offlineUntilMs_ = 0;
    updateDownloadStatus();
    downloadStep();
    return {};
}

void MapModel::stopDownload() {
    if (!download_.active) {
        return;
    }
    download_.cancelled = true;
    if (downloadReply_) {
        downloadReply_->abort(); // finishes it, which ends the download
    }
}

/// One tile at a time, a few a second: a courtesy to the tile server. Tiles already stored are
/// counted and skipped.
void MapModel::downloadStep() {
    if (!download_.active) {
        return;
    }
    while (download_.next < download_.tiles.size() && isStored(download_.style, download_.tiles[download_.next])) {
        download_.done += 1;
        download_.next += 1;
    }
    if (download_.cancelled || download_.next >= download_.tiles.size() || download_.failedInARow >= kGiveUpAfter) {
        downloadFinished();
        return;
    }
    updateDownloadStatus();
    const TileId id = download_.tiles[download_.next];
    download_.next += 1;
    QNetworkReply* reply = get(download_.style, id);
    downloadReply_ = reply;
    const QString style = download_.style;
    connect(reply, &QNetworkReply::finished, this, [this, reply, style, id] {
        reply->deleteLater();
        if (store(reply, style, id)) {
            download_.done += 1;
            download_.failedInARow = 0;
            missing_.remove(TileMath::relativePath(style, id));
        } else if (!download_.cancelled) {
            download_.failed += 1;
            download_.failedInARow += 1;
        }
        updateDownloadStatus();
        QTimer::singleShot(kPoliteMs, this, &MapModel::downloadStep);
    });
}

void MapModel::updateDownloadStatus() {
    downloadStatus_ = QStringLiteral("Downloading %1: %2 / %3").arg(download_.style).arg(download_.done).arg(download_.tiles.size());
    if (download_.failed > 0) {
        downloadStatus_ += QStringLiteral(" · %1 failed").arg(download_.failed);
    }
    emit downloadChanged();
}

void MapModel::downloadFinished() {
    const Download finished = download_;
    download_ = {};
    downloadReply_.clear();
    const qsizetype total = finished.tiles.size();
    if (finished.done == total) {
        downloadStatus_ = QStringLiteral("Done: %1 %2 tiles around %3, %4 (zoom %5 to %6)")
                              .arg(finished.done)
                              .arg(finished.style, degrees(finished.lat), degrees(finished.lon))
                              .arg(TileMath::kDownloadMinZoom)
                              .arg(finished.topZoom);
    } else if (finished.cancelled) {
        downloadStatus_ = QStringLiteral("Stopped at %1 of %2").arg(finished.done).arg(total);
    } else {
        downloadStatus_ = QStringLiteral("Stopped at %1 of %2: no internet? Connect this computer to a network with "
                                         "internet and try again")
                              .arg(finished.done)
                              .arg(total);
    }
    emit downloadChanged();
    revision_ += 1;
    emit revisionChanged();
    refreshStored();
}

// MARK: Store

void MapModel::runOnStore(std::function<void()> work) {
    storeJobs_ += 1;
    emit storedChanged();
    pool_.start(std::move(work));
}

void MapModel::refreshStored() {
    const QString base = root();
    QPointer<MapModel> self(this);
    runOnStore([self, base] {
        QStringList parts;
        for (const QString& style : {MapSettings::kSatellite, MapSettings::kStreet}) {
            int count = 0;
            qint64 bytes = 0;
            QDirIterator it(base + QLatin1Char('/') + style, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo info = it.nextFileInfo();
                count += 1;
                bytes += info.size();
            }
            parts.append(QStringLiteral("%1 %2 tiles (%3)").arg(style).arg(count).arg(megabytes(bytes)));
        }
        const QString text = QStringLiteral("On this computer: ") + parts.join(QStringLiteral(" · "));
        // The model waits for this pool when it goes, so it is still there.
        QMetaObject::invokeMethod(self.data(), [self, text] {
            if (!self) return;
            self->storeJobs_ -= 1;
            self->storedText_ = text;
            emit self->storedChanged();
        }, Qt::QueuedConnection);
    });
}

void MapModel::deleteTiles(const QString& style) {
    if (download_.active || (style != MapSettings::kSatellite && style != MapSettings::kStreet)) {
        return;
    }
    const QString folder = root() + QLatin1Char('/') + style;
    QPointer<MapModel> self(this);
    runOnStore([self, folder] {
        QDir(folder).removeRecursively();
        QMetaObject::invokeMethod(self.data(), [self] {
            if (!self) return;
            self->storeJobs_ -= 1;
            self->missing_.clear();
            self->revision_ += 1;
            emit self->revisionChanged();
            self->refreshStored();
        }, Qt::QueuedConnection);
    });
}
