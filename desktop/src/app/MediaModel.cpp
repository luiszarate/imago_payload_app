#include "app/MediaModel.h"

#include "core/PayloadSession.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

/// ~7 Hz: progress and arriving thumbnails look live, and a poll is a copy of a small struct.
constexpr int kPollIntervalMs = 150;
/// The payload answers 404 while it detects its cameras (Gremsy's SDK).
constexpr int kStartingRetryMs = 5000;
const char* kFolderKey = "media.folder";

QString defaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Imago Payload Control");
}

} // namespace

// MARK: Thumbnails

void MediaThumbnails::setSession(PayloadSession* session) {
    {
        QMutexLocker lock(&sessionMutex_);
        session_ = session;
    }
    QMutexLocker lock(&cacheMutex_);
    cache_.clear(); // another payload, other files
}

QImage MediaThumbnails::image(const QString& name, const QSize& requested) {
    const QString key = QStringLiteral("%1@%2x%3").arg(name).arg(requested.width()).arg(requested.height());
    {
        QMutexLocker lock(&cacheMutex_);
        if (const QImage* hit = cache_.object(key)) {
            return *hit;
        }
    }
    QByteArray bytes;
    {
        QMutexLocker lock(&sessionMutex_);
        if (session_ != nullptr) {
            bytes = session_->mediaThumbnail(name);
        }
    }
    if (bytes.isEmpty()) {
        return {};
    }
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    // Scaled while decoding (libjpeg's DCT scaling): just enough to cover the cell, which crops.
    if (const QSize size = reader.size(); size.isValid() && (requested.width() > 0 || requested.height() > 0)) {
        const double sx = requested.width() > 0 ? double(requested.width()) / size.width() : 0.0;
        const double sy = requested.height() > 0 ? double(requested.height()) / size.height() : 0.0;
        const double scale = std::max(sx, sy);
        if (scale < 1.0) {
            reader.setScaledSize(QSize(std::max(1, int(std::ceil(size.width() * scale))),
                                       std::max(1, int(std::ceil(size.height() * scale)))));
        }
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    QMutexLocker lock(&cacheMutex_);
    cache_.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes()));
    return image;
}

namespace {

class MediaImageResponse : public QQuickImageResponse, public QRunnable {
  public:
    MediaImageResponse(std::shared_ptr<MediaThumbnails> thumbnails, QString name, QSize size)
        : thumbnails_(std::move(thumbnails)), name_(std::move(name)), size_(size) {
        setAutoDelete(false);
    }
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    QString errorString() const override { return image_.isNull() ? QStringLiteral("No thumbnail") : QString(); }
    void run() override {
        image_ = thumbnails_->image(name_, size_);
        emit finished();
    }

  private:
    std::shared_ptr<MediaThumbnails> thumbnails_;
    QString name_;
    QSize size_;
    QImage image_;
};

} // namespace

MediaImageProvider::MediaImageProvider(std::shared_ptr<MediaThumbnails> thumbnails)
    : thumbnails_(std::move(thumbnails)) {
    // Decoding a few-KB JPEG takes a millisecond; two threads keep up with a scrolling sheet.
    pool_.setMaxThreadCount(2);
}

QQuickImageResponse* MediaImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new MediaImageResponse(thumbnails_, QUrl::fromPercentEncoding(id.toUtf8()), requestedSize);
    pool_.start(response);
    return response;
}

// MARK: List models

int MediaFilesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant MediaFilesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[index.row()];
    const Media::File& f = row.file;
    switch (role) {
    case NameRole:
        return f.name;
    case KindRole:
        return f.kind;
    case VideoRole:
        return f.isVideo();
    case ThermalRole:
        return f.thermal;
    case RadiometricRole:
        return f.kind == IMAGO_MEDIA_RADIOMETRIC;
    case SizeTextRole:
        return Media::sizeText(f.size);
    case TimeTextRole:
        return Media::captureTimeText(f.time);
    case ThumbnailUrlRole:
        return f.thumbnail ? QStringLiteral("image://media/") + QString::fromLatin1(QUrl::toPercentEncoding(f.name))
                           : QString();
    case ThumbnailFailedRole:
        return f.thumbnailFailed;
    case OnDeviceRole:
        return row.onDevice;
    case SelectedRole:
        return row.selected;
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaFilesModel::roleNames() const {
    return {{NameRole, "name"},
            {KindRole, "kind"},
            {VideoRole, "video"},
            {ThermalRole, "thermal"},
            {RadiometricRole, "radiometric"},
            {SizeTextRole, "sizeText"},
            {TimeTextRole, "timeText"},
            {ThumbnailUrlRole, "thumbnailUrl"},
            {ThumbnailFailedRole, "thumbnailFailed"},
            {OnDeviceRole, "onDevice"},
            {SelectedRole, "selected"}};
}

void MediaFilesModel::setRows(QList<Row> rows) {
    const bool sameFiles = rows.size() == rows_.size() &&
                           std::equal(rows.begin(), rows.end(), rows_.begin(),
                                      [](const Row& a, const Row& b) { return a.file.name == b.file.name; });
    if (!sameFiles) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (!(rows[i] == rows_[i])) {
            rows_[i] = rows[i];
            emit dataChanged(index(i), index(i));
        }
    }
}

int MediaFlightsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant MediaFlightsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[index.row()];
    switch (role) {
    case DateTextRole:
        return Media::flightDateText(row.flight);
    case TimeTextRole:
        return Media::flightTimeText(row.flight);
    case CountsTextRole:
        return Media::countsText(row.flight.photos, row.flight.videos);
    case SizeTextRole:
        return Media::sizeText(row.flight.size);
    case HasTimeRole:
        return row.flight.hasTime();
    case PhotoMarksRole:
        return row.photoMarks;
    case VideoMarksRole:
        return row.videoMarks;
    case CurrentRole:
        return row.current;
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaFlightsModel::roleNames() const {
    return {{DateTextRole, "dateText"},     {TimeTextRole, "timeText"},     {CountsTextRole, "countsText"},
            {SizeTextRole, "sizeText"},     {HasTimeRole, "hasTime"},       {PhotoMarksRole, "photoMarks"},
            {VideoMarksRole, "videoMarks"}, {CurrentRole, "current"}};
}

void MediaFlightsModel::setRows(QList<Row> rows) {
    if (rows.size() != rows_.size()) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (!(rows[i] == rows_[i])) {
            rows_[i] = rows[i];
            emit dataChanged(index(i), index(i));
        }
    }
}

// MARK: Media

MediaModel::MediaModel(QSettings& settings, QObject* parent)
    : QObject(parent),
      settings_(settings),
      thumbnails_(std::make_shared<MediaThumbnails>()),
      filesModel_(this),
      flightsModel_(this) {
    folder_ = settings_.value(QLatin1String(kFolderKey)).toString();
    if (folder_.isEmpty()) {
        folder_ = defaultFolder();
    }
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &MediaModel::poll);
    retryTimer_.setSingleShot(true);
    retryTimer_.setInterval(kStartingRetryMs);
    connect(&retryTimer_, &QTimer::timeout, this, [this] {
        if (open_ && state_.status == IMAGO_MEDIA_STATUS_STARTING) refresh();
    });
}

MediaModel::~MediaModel() { thumbnails_->setSession(nullptr); }

void MediaModel::setSession(PayloadSession* session) {
    if (session == session_) {
        return;
    }
    thumbnails_->setSession(session);
    session_ = session;
    const bool wasPaused = videoPaused_;
    // A new session (or none): nothing of the old one carries over, not even a paused video,
    // which the new session starts by itself.
    reset();
    if (session_ != nullptr) {
        session_->setMediaActive(open_);
        if (open_) {
            refresh();
            pollTimer_.start();
        }
    } else {
        pollTimer_.stop();
    }
    emit stateChanged();
    emit currentFlightChanged();
    emit selectionChanged();
    emit transferChanged();
    if (wasPaused) emit videoPausedChanged();
    emit previewChanged();
}

void MediaModel::reset() {
    retryTimer_.stop();
    state_ = {};
    revision_ = 0;
    noWebHost_ = false;
    announcedFiles_ = false;
    error_.clear();
    storageText_.clear();
    storageFraction_ = -1.0;
    storages_.clear();
    files_.clear();
    flights_.clear();
    fileIndex_.clear();
    exists_.clear();
    currentFlight_ = 0;
    currentFlightFile_.clear();
    followLatest_ = true;
    selection_.clear();
    pending_.clear();
    batch_ = {};
    batchShown_ = false;
    transfer_.clear();
    notice_.clear();
    videoPaused_ = false;
    previewName_.clear();
    previewSource_.clear();
    previewState_.clear();
    previewProgress_ = 0.0;
    previewDownloadId_ = 0;
    previewDownloadName_.clear();
    filesModel_.setRows({});
    flightsModel_.setRows({});
}

void MediaModel::setOpen(bool open) {
    if (open == open_) {
        return;
    }
    open_ = open;
    if (open_) {
        announcedFiles_ = false;
        followLatest_ = true;
        // Previews of an earlier visit, unless one is still being written.
        if (pending_.isEmpty()) {
            QDir(QFileInfo(cachePath(QStringLiteral("x"))).path()).removeRecursively();
        }
        if (session_ != nullptr) {
            session_->setMediaActive(true);
            refresh();
            pollTimer_.start();
            poll();
        }
    } else {
        pollTimer_.stop();
        retryTimer_.stop();
        if (session_ != nullptr) session_->setMediaActive(false);
        // Downloads go on without the window; the live video comes back now.
        resumeVideo();
        closePreview();
        selection_.clear();
        updateSelectionRows();
        emit selectionChanged();
    }
    emit openChanged();
}

void MediaModel::refresh() {
    if (session_ == nullptr) {
        return;
    }
    setNotice({});
    const bool noHost = session_->refreshMedia() == 0;
    if (noHost != noWebHost_) {
        noWebHost_ = noHost;
        error_ = noHost ? QStringLiteral("The payload's web page needs the payload IP in the connection profile.")
                        : QString();
        emit stateChanged();
    }
}

void MediaModel::setStorage(int index) {
    if (session_ != nullptr && index >= 0 && index < storages_.size() && index != state_.storage_current) {
        session_->setMediaStorage(index);
    }
}

void MediaModel::requestThumbnail(const QString& name) {
    if (session_ != nullptr) session_->requestMediaThumbnail(name);
}

// MARK: Polling

void MediaModel::poll() {
    if (session_ == nullptr) {
        return;
    }
    const imago_media_state s = session_->mediaState();
    const imago_state app = session_->state();
    const imago_media_status oldStatus = state_.status;
    const bool listChanged = s.revision != revision_ || s.file_count != state_.file_count ||
                             s.flight_count != state_.flight_count;
    const bool stateDiffers = s.status != state_.status || s.storage_count != state_.storage_count ||
                              s.storage_current != state_.storage_current ||
                              (s.status == IMAGO_MEDIA_STATUS_FAILED && std::strcmp(s.error, state_.error) != 0);
    state_ = s;

    bool changed = stateDiffers;
    if (!noWebHost_) {
        const QString error = s.status == IMAGO_MEDIA_STATUS_FAILED ? QString::fromUtf8(s.error) : QString();
        if (error != error_) {
            error_ = error;
            changed = true;
        }
    }
    QStringList storages;
    for (uint32_t i = 0; i < std::min<uint32_t>(s.storage_count, IMAGO_MEDIA_MAX_STORAGES); ++i) {
        storages << QString::fromUtf8(s.storage_label[i]);
    }
    if (storages != storages_) {
        storages_ = storages;
        changed = true;
    }
    QString storageText;
    double storageFraction = -1.0;
    if (app.storage_valid && app.storage_total_mib > 0) {
        const double total = app.storage_total_mib;
        const double used = std::clamp(total - double(app.storage_available_mib), 0.0, total);
        storageText = Media::storageText(used, total);
        storageFraction = used / total;
    }
    if (storageText != storageText_) {
        storageText_ = storageText;
        storageFraction_ = storageFraction;
        changed = true;
    }
    if (s.status == IMAGO_MEDIA_STATUS_STARTING && oldStatus != IMAGO_MEDIA_STATUS_STARTING) {
        retryTimer_.start();
    }
    if (listChanged) {
        revision_ = s.revision;
        reload();
        changed = true;
    }
    if (changed) {
        emit stateChanged();
    }
    updateTransfer();
    if (!announcedFiles_ && s.status == IMAGO_MEDIA_STATUS_READY && !files_.empty()) {
        announcedFiles_ = true;
        emit filesReady();
    }
}

void MediaModel::reload() {
    files_.clear();
    for (const imago_media_file& f : session_->mediaFiles()) {
        files_.push_back(Media::File::from(f));
    }
    flights_.clear();
    for (const imago_media_flight& f : session_->mediaFlights()) {
        flights_.push_back(Media::Flight::from(f));
    }
    fileIndex_.clear();
    for (int i = 0; i < int(files_.size()); ++i) {
        fileIndex_.insert(files_[i].name, i);
    }

    // The same flight as before: the one that now holds its first file.
    const int previous = currentFlight_;
    int row = 0;
    if (!followLatest_) {
        if (const Media::File* file = fileNamed(currentFlightFile_)) {
            row = file->flight;
        }
    }
    currentFlight_ = flights_.empty() ? 0 : std::clamp(row, 0, int(flights_.size()) - 1);
    if (!flights_.empty()) {
        const Media::Flight& flight = flights_[currentFlight_];
        if (flight.firstFile < int(files_.size())) currentFlightFile_ = files_[flight.firstFile].name;
    }
    rebuildRows();
    if (currentFlight_ != previous) {
        emit currentFlightChanged();
    }
    emit previewChanged(); // its row or its file may have moved
}

void MediaModel::rebuildRows() {
    QList<MediaFlightsModel::Row> flightRows;
    for (int i = 0; i < int(flights_.size()); ++i) {
        MediaFlightsModel::Row row;
        row.flight = flights_[i];
        row.current = i == currentFlight_;
        const int end = std::min<int>(row.flight.firstFile + row.flight.fileCount, int(files_.size()));
        for (int f = row.flight.firstFile; f < end; ++f) {
            const double at = Media::timelineFraction(files_[f].time, row.flight);
            if (at >= 0) {
                (files_[f].isVideo() ? row.videoMarks : row.photoMarks).append(at);
            }
        }
        flightRows.append(row);
    }
    flightsModel_.setRows(flightRows);

    const QStringList names = visibleNames();
    const int selected = selection_.count();
    selection_.retain(names);
    QList<MediaFilesModel::Row> rows;
    rows.reserve(names.size());
    for (const QString& name : names) {
        const Media::File& file = files_[fileIndex_.value(name)];
        rows.append({file, onDevice(file), selection_.contains(name)});
    }
    filesModel_.setRows(rows);
    if (selection_.count() != selected) {
        emit selectionChanged();
    }
}

void MediaModel::updateSelectionRows() {
    QList<MediaFilesModel::Row> rows = filesModel_.rows();
    for (MediaFilesModel::Row& row : rows) {
        row.selected = selection_.contains(row.file.name);
    }
    filesModel_.setRows(rows);
}

QStringList MediaModel::visibleNames() const {
    QStringList names;
    if (flights_.empty() || currentFlight_ >= int(flights_.size())) {
        return names;
    }
    const Media::Flight& flight = flights_[currentFlight_];
    const int end = std::min<int>(flight.firstFile + flight.fileCount, int(files_.size()));
    for (int i = flight.firstFile; i < end; ++i) {
        if (Media::matches(files_[i], filter_)) names << files_[i].name;
    }
    return names;
}

const Media::Flight* MediaModel::flightOf(const Media::File& file) const {
    return file.flight >= 0 && file.flight < int(flights_.size()) ? &flights_[file.flight] : nullptr;
}

const Media::File* MediaModel::fileNamed(const QString& name) const {
    const auto it = fileIndex_.constFind(name);
    return it == fileIndex_.cend() ? nullptr : &files_[*it];
}

QString MediaModel::pathFor(const Media::File& file) const {
    const Media::Flight* flight = flightOf(file);
    return Media::downloadPath(folder_, flight != nullptr ? *flight : Media::Flight{}, file.name);
}

bool MediaModel::onDevice(const Media::File& file) const {
    const QString path = pathFor(file);
    auto it = exists_.constFind(path);
    if (it == exists_.cend()) {
        it = exists_.insert(path, QFileInfo::exists(path));
    }
    return *it;
}

QString MediaModel::cachePath(const QString& name) const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/Media/") +
           QFileInfo(name).fileName();
}

// MARK: Selection

void MediaModel::setCurrentFlight(int row) {
    if (row == currentFlight_ || row < 0 || row >= int(flights_.size())) {
        return;
    }
    currentFlight_ = row;
    followLatest_ = row == 0;
    const Media::Flight& flight = flights_[row];
    if (flight.firstFile < int(files_.size())) currentFlightFile_ = files_[flight.firstFile].name;
    selection_.clear();
    closePreview();
    setNotice({});
    rebuildRows();
    emit currentFlightChanged();
    emit selectionChanged();
}

void MediaModel::setFilter(int filter) {
    if (filter == filter_) {
        return;
    }
    filter_ = filter;
    rebuildRows();
    emit currentFlightChanged();
    emit selectionChanged();
}

void MediaModel::click(int row, bool toggle, bool range) {
    selection_.click(visibleNames(), row, toggle, range);
    // A new selection is the next action: the finished transfer row gives way.
    if (!batch_.running()) batchShown_ = false;
    setNotice({});
    updateTransfer();
    updateSelectionRows();
    emit selectionChanged();
}

void MediaModel::selectForMenu(int row) {
    const QStringList names = visibleNames();
    if (row >= 0 && row < names.size() && !selection_.contains(names[row])) {
        click(row, false, false);
    }
}

void MediaModel::selectAll() {
    selection_.selectAll(visibleNames());
    if (!batch_.running()) batchShown_ = false;
    setNotice({});
    updateTransfer();
    updateSelectionRows();
    emit selectionChanged();
}

void MediaModel::clearSelection() {
    selection_.clear();
    updateSelectionRows();
    emit selectionChanged();
}

int MediaModel::rowNearest(double fraction) const {
    if (flights_.empty()) {
        return -1;
    }
    QList<qint64> times;
    for (const MediaFilesModel::Row& row : filesModel_.rows()) {
        times << row.file.time;
    }
    return Media::nearestIndex(times, flights_[currentFlight_], fraction);
}

QString MediaModel::selectionText() const {
    qint64 bytes = 0;
    for (const MediaFilesModel::Row& row : filesModel_.rows()) {
        if (row.selected && row.file.size > 0) bytes += row.file.size;
    }
    const QString count = QStringLiteral("%1 selected").arg(selection_.count());
    return bytes > 0 ? count + QStringLiteral(" · ") + Media::sizeText(bytes) : count;
}

// MARK: Downloads

QList<MediaModel::Request> MediaModel::toFetch(const QStringList& names) const {
    QList<Request> requests;
    for (const QString& name : names) {
        const Media::File* file = fileNamed(name);
        // Already here: the core writes "<path>.part" and renames it, so a file at path is whole.
        if (file != nullptr && !onDevice(*file)) {
            requests.append({file->name, pathFor(*file), file->size});
        }
    }
    return requests;
}

int MediaModel::selectionToFetch() const { return int(toFetch(selection_.ordered(visibleNames())).size()); }

int MediaModel::flightToFetch(int flightRow) const {
    if (flightRow < 0 || flightRow >= int(flights_.size())) {
        return 0;
    }
    const Media::Flight& flight = flights_[flightRow];
    QStringList names;
    for (int i = flight.firstFile; i < std::min<int>(flight.firstFile + flight.fileCount, int(files_.size())); ++i) {
        names << files_[i].name;
    }
    return int(toFetch(names).size());
}

void MediaModel::downloadSelection(bool pauseVideo) {
    const QStringList names = selection_.ordered(visibleNames());
    const QList<Request> requests = toFetch(names);
    if (requests.isEmpty() && !names.isEmpty()) {
        setNotice(names.size() == 1 ? QStringLiteral("Already on this device.")
                                    : QStringLiteral("All %1 are already on this device.").arg(names.size()));
        return;
    }
    startDownloads(requests, pauseVideo);
}

void MediaModel::downloadFlight(int flightRow, bool pauseVideo) {
    if (flightRow < 0 || flightRow >= int(flights_.size())) {
        return;
    }
    const Media::Flight& flight = flights_[flightRow];
    QStringList names;
    for (int i = flight.firstFile; i < std::min<int>(flight.firstFile + flight.fileCount, int(files_.size())); ++i) {
        names << files_[i].name;
    }
    const QList<Request> requests = toFetch(names);
    if (requests.isEmpty()) {
        setNotice(QStringLiteral("This flight is already on this device."));
        return;
    }
    startDownloads(requests, pauseVideo);
}

void MediaModel::downloadPreviewed(bool pauseVideo) {
    const Media::File* file = fileNamed(previewName_);
    if (file == nullptr || onDevice(*file)) {
        return;
    }
    // The preview already brought the photo here: a copy, not a second trip over the datalink.
    const QString cached = cachePath(file->name);
    const QString path = pathFor(*file);
    if (file->isPhoto() && QFileInfo::exists(cached)) {
        QDir().mkpath(QFileInfo(path).path());
        if (QFile::copy(cached, path)) {
            exists_.insert(path, true);
            rebuildRows();
            emit previewChanged();
            return;
        }
    }
    startDownloads({{file->name, path, file->size}}, pauseVideo);
}

void MediaModel::startDownloads(const QList<Request>& requests, bool pauseVideo) {
    if (session_ == nullptr || requests.isEmpty()) {
        return;
    }
    setNotice({});
    if (!batch_.running()) {
        batch_ = {}; // a new batch; files asked for while one runs join it
    }
    batchShown_ = true;
    if (pauseVideo && !videoPaused_) {
        session_->stopVideo();
        videoPaused_ = true;
        emit videoPausedChanged();
    }
    for (const Request& r : requests) {
        batch_.total += 1;
        const uint32_t id = session_->downloadMedia(r.name, r.path);
        if (id == 0) {
            batch_.failed += 1;
            batch_.error = QStringLiteral("The payload's web page needs the payload IP in the connection profile.");
            continue;
        }
        pending_.insert(id, {r.name, r.path, r.size, false});
        if (r.size > 0) batch_.totalBytes += r.size;
    }
    if (!batch_.running()) {
        batchEnded();
    }
    updateTransfer();
}

void MediaModel::cancelDownloads() {
    if (session_ != nullptr) session_->cancelMediaDownloads();
}

void MediaModel::dismissTransfer() {
    batchShown_ = false;
    setNotice({});
    updateTransfer();
}

void MediaModel::batchEnded() { resumeVideo(); }

void MediaModel::resumeVideo() {
    if (!videoPaused_) {
        return;
    }
    videoPaused_ = false;
    if (session_ != nullptr) session_->startVideo();
    emit videoPausedChanged();
}

void MediaModel::updateTransfer() {
    QVariantMap t;
    QString current;
    bool ours = false;
    if (state_.downloading) {
        current = QString::fromUtf8(state_.download_name);
        for (const Pending& p : std::as_const(pending_)) {
            if (!p.preview && p.name == current) {
                ours = true;
                break;
            }
        }
    }
    if (batch_.running()) {
        const double fraction = batch_.fraction(ours ? state_.download_bytes : 0);
        t = {{QStringLiteral("state"), QStringLiteral("running")},
             {QStringLiteral("title"), batch_.runningTitle()},
             {QStringLiteral("name"), ours ? current : QString()},
             {QStringLiteral("fraction"), fraction},
             {QStringLiteral("percent"), QStringLiteral("%1%").arg(int(std::floor(fraction * 100.0)))},
             {QStringLiteral("rate"), ours && state_.download_bytes_per_s > 0
                                          ? Media::rateText(state_.download_bytes_per_s) : QString()}};
    } else if (batchShown_ && !batch_.isEmpty()) {
        t = {{QStringLiteral("state"), QStringLiteral("finished")},
             {QStringLiteral("title"), batch_.finishedTitle()},
             {QStringLiteral("detail"), batch_.finishedDetail()},
             {QStringLiteral("error"), batch_.failed > 0 ? batch_.error : QString()},
             {QStringLiteral("failed"), batch_.failed > 0}};
    }
    t.insert(QStringLiteral("deleting"), int(state_.deletes_pending));
    if (t != transfer_) {
        transfer_ = t;
        emit transferChanged();
    }

    // The preview's own download: its progress, or behind the batch.
    if (previewState_ == QLatin1String("loading") || previewState_ == QLatin1String("waiting")) {
        const bool itsTurn = state_.downloading && current == previewDownloadName_;
        const QString stateText = !state_.downloading || itsTurn ? QStringLiteral("loading") : QStringLiteral("waiting");
        const double progress = itsTurn && state_.download_total > 0
                                    ? double(state_.download_bytes) / double(state_.download_total) : 0.0;
        if (stateText != previewState_ || progress != previewProgress_) {
            previewState_ = stateText;
            previewProgress_ = progress;
            emit previewChanged();
        }
    }
}

void MediaModel::handleResult(uint32_t requestId, uint32_t op, int32_t result, const QString& text) {
    if (op == IMAGO_MEDIA_OP_DOWNLOAD) {
        const auto it = pending_.constFind(requestId);
        if (it == pending_.cend()) {
            return;
        }
        const Pending p = *it;
        pending_.erase(it);
        const bool ok = result == IMAGO_RESULT_ACCEPTED;
        if (ok) {
            exists_.insert(p.path, true);
        }
        if (p.preview) {
            previewDownloadId_ = 0;
            previewDownloadName_.clear();
            if (p.name == previewName_) {
                previewState_ = ok ? QStringLiteral("ready") : QStringLiteral("failed");
                previewSource_ = ok ? QUrl::fromLocalFile(p.path) : QUrl();
            } else if (previewState_ == QLatin1String("loading") || previewState_ == QLatin1String("waiting")) {
                startPreviewDownload(); // the one the operator moved on to
            }
            emit previewChanged();
        } else {
            if (ok) {
                batch_.done += 1;
            } else if (result == IMAGO_RESULT_CANCELLED) {
                batch_.cancelled += 1;
            } else {
                batch_.failed += 1;
                const QString why = session_ != nullptr ? QString::fromUtf8(session_->mediaState().error) : QString();
                batch_.error = why.isEmpty() ? text + QStringLiteral(": no answer from the payload") : why;
            }
            batch_.finishedBytes += std::max<qint64>(p.size, 0);
            if (!batch_.running()) {
                batchEnded();
            }
            if (ok) {
                rebuildRows();
                if (p.name == previewName_) emit previewChanged();
            }
        }
        updateTransfer();
        emit transferChanged(); // downloading may have changed
    } else if (op == IMAGO_MEDIA_OP_DELETE && result != IMAGO_RESULT_ACCEPTED) {
        const QString why = session_ != nullptr ? QString::fromUtf8(session_->mediaState().error) : QString();
        setNotice(why.isEmpty() ? QStringLiteral("Could not delete ") + text : QStringLiteral("Could not delete ") + why);
    }
    // The list and its state arrive through the next poll.
}

void MediaModel::setNotice(const QString& notice) {
    if (notice != notice_) {
        notice_ = notice;
        emit transferChanged();
    }
}

// MARK: Deletes

void MediaModel::deleteSelection() {
    if (session_ == nullptr) {
        return;
    }
    setNotice({});
    const QStringList names = selection_.ordered(visibleNames());
    for (const QString& name : names) {
        session_->deleteMedia(name);
    }
    selection_.clear();
    updateSelectionRows();
    emit selectionChanged();
}

void MediaModel::deletePreviewed() {
    if (session_ == nullptr || previewName_.isEmpty()) {
        return;
    }
    setNotice({});
    const QStringList names = visibleNames();
    const qsizetype row = names.indexOf(previewName_);
    session_->deleteMedia(previewName_);
    // On to the next one, as a photo viewer does.
    const qsizetype next = row + 1 < names.size() ? row + 1 : row - 1;
    if (row >= 0 && next >= 0 && next != row) {
        showPreview(names[next]);
    } else {
        closePreview();
    }
}

// MARK: Preview

int MediaModel::previewRow() const {
    if (previewName_.isEmpty()) {
        return -1;
    }
    const QList<MediaFilesModel::Row>& rows = filesModel_.rows();
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].file.name == previewName_) return i;
    }
    return -1;
}

QVariantMap MediaModel::previewFile() const {
    const Media::File* file = fileNamed(previewName_);
    if (file == nullptr) {
        return {};
    }
    return {{QStringLiteral("name"), file->name},
            {QStringLiteral("timeText"), Media::captureTimeText(file->time)},
            {QStringLiteral("sizeText"), Media::sizeText(file->size)},
            {QStringLiteral("video"), file->isVideo()},
            {QStringLiteral("radiometric"), file->kind == IMAGO_MEDIA_RADIOMETRIC},
            {QStringLiteral("thermal"), file->thermal},
            {QStringLiteral("onDevice"), onDevice(*file)}};
}

void MediaModel::openPreview(int row) {
    const QStringList names = visibleNames();
    if (row >= 0 && row < names.size()) {
        showPreview(names[row]);
    }
}

void MediaModel::stepPreview(int step) {
    const int row = previewRow();
    if (row >= 0) openPreview(std::clamp(row + step, 0, int(filesModel_.rows().size()) - 1));
}

void MediaModel::showPreview(const QString& name) {
    const Media::File* file = fileNamed(name);
    if (file == nullptr) {
        closePreview();
        return;
    }
    previewName_ = name;
    previewProgress_ = 0.0;
    previewSource_.clear();
    if (!file->isPhoto()) {
        // A video, or the radiometric TIFF (Qt has no TIFF reader here): a tile, no picture.
        previewState_ = QStringLiteral("tile");
    } else {
        const QString local = onDevice(*file) ? pathFor(*file) : cachePath(name);
        if (QFileInfo::exists(local)) {
            previewState_ = QStringLiteral("ready");
            previewSource_ = QUrl::fromLocalFile(local);
        } else {
            previewState_ = QStringLiteral("loading");
            startPreviewDownload();
        }
    }
    emit previewChanged();
    updateTransfer();
}

void MediaModel::startPreviewDownload() {
    // One at a time: paging through ten photos must not queue ten full downloads.
    if (session_ == nullptr || previewDownloadId_ != 0 || previewName_.isEmpty()) {
        return;
    }
    const Media::File* file = fileNamed(previewName_);
    const QString path = cachePath(previewName_);
    const uint32_t id = session_->downloadMedia(previewName_, path);
    if (id == 0) {
        previewState_ = QStringLiteral("failed");
        return;
    }
    pending_.insert(id, {previewName_, path, file != nullptr ? file->size : -1, true});
    previewDownloadId_ = id;
    previewDownloadName_ = previewName_;
    emit transferChanged();
}

void MediaModel::closePreview() {
    if (previewName_.isEmpty() && previewState_.isEmpty()) {
        return;
    }
    previewName_.clear();
    previewSource_.clear();
    previewState_.clear();
    previewProgress_ = 0.0;
    emit previewChanged();
}

void MediaModel::openPreviewedFile() {
    const Media::File* file = fileNamed(previewName_);
    if (file != nullptr && onDevice(*file)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(pathFor(*file)));
    }
}

// MARK: Folder

void MediaModel::setFolder(const QUrl& folder) {
    const QString path = folder.toLocalFile();
    if (path.isEmpty() || path == folder_) {
        return;
    }
    folder_ = path;
    settings_.setValue(QLatin1String(kFolderKey), path);
    exists_.clear();
    rebuildRows();
    emit folderChanged();
    emit previewChanged();
}

void MediaModel::overrideFolder(const QString& folder) {
    folder_ = folder;
    exists_.clear();
    emit folderChanged();
}

void MediaModel::openFolder() {
    QDir().mkpath(folder_);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_));
}
