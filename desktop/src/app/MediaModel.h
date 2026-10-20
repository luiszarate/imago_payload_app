#pragma once

#include "core/MediaLibrary.h"

#include <QAbstractListModel>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQmlEngine>
#include <QQuickAsyncImageProvider>
#include <QSettings>
#include <QSize>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

class PayloadSession;

/// Thumbnails for the image provider: the core's JPEG bytes, decoded and scaled down to the
/// cell on the provider's threads (never the GUI thread), kept by file name and size.
class MediaThumbnails {
  public:
    /// Main thread. Waits for a copy in progress, so the session can go right after.
    void setSession(PayloadSession* session);
    /// Any thread. Null while the core has no thumbnail for the file.
    QImage image(const QString& name, const QSize& requested);

  private:
    QMutex sessionMutex_;
    PayloadSession* session_ = nullptr;
    QMutex cacheMutex_;
    /// Cost in bytes: a few hundred cells at their size.
    QCache<QString, QImage> cache_{48 * 1024 * 1024};
};

/// "image://media/<percent-encoded name>"
class MediaImageProvider : public QQuickAsyncImageProvider {
  public:
    explicit MediaImageProvider(std::shared_ptr<MediaThumbnails> thumbnails);
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

  private:
    std::shared_ptr<MediaThumbnails> thumbnails_;
    QThreadPool pool_;
};

/// The selected flight's files as the sheet shows them (filtered).
class MediaFilesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by App.media")

  public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        KindRole,
        VideoRole,
        ThermalRole,
        RadiometricRole,
        SizeTextRole,
        TimeTextRole,
        ThumbnailUrlRole,
        ThumbnailFailedRole,
        OnDeviceRole,
        SelectedRole,
    };
    struct Row {
        Media::File file;
        bool onDevice = false;
        bool selected = false;
        bool operator==(const Row&) const = default;
    };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Same files in the same order: only the changed rows are reported, so the sheet keeps its
    /// cells and its scroll position while thumbnails arrive.
    void setRows(QList<Row> rows);
    const QList<Row>& rows() const { return rows_; }

  private:
    QList<Row> rows_;
};

class MediaFlightsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by App.media")

  public:
    enum Role {
        DateTextRole = Qt::UserRole + 1,
        TimeTextRole,
        CountsTextRole,
        SizeTextRole,
        HasTimeRole,
        PhotoMarksRole,
        VideoMarksRole,
        CurrentRole,
    };
    struct Row {
        Media::Flight flight;
        QVariantList photoMarks; ///< 0..1 along the flight
        QVariantList videoMarks;
        bool current = false;
        bool operator==(const Row&) const = default;
    };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRows(QList<Row> rows);

  private:
    QList<Row> rows_;
};

/// The Media window (App.media): the payload's photos and videos by flight, the selection,
/// downloads with their transfer row, deletes and the large preview. Main thread; the core does
/// the network work on its own threads, so nothing here waits for it.
class MediaModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.media")

    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    /// imago_media_status
    Q_PROPERTY(int status READ status NOTIFY stateChanged)
    /// Why the list could not be read (failed), from the core.
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged)
    Q_PROPERTY(QString storageText READ storageText NOTIFY stateChanged)
    /// Used / total, 0..1; -1 unknown.
    Q_PROPERTY(double storageFraction READ storageFraction NOTIFY stateChanged)
    Q_PROPERTY(QStringList storages READ storages NOTIFY stateChanged)
    Q_PROPERTY(int currentStorage READ currentStorage NOTIFY stateChanged)

    Q_PROPERTY(MediaFlightsModel* flights READ flights CONSTANT)
    Q_PROPERTY(MediaFilesModel* files READ files CONSTANT)
    Q_PROPERTY(int currentFlight READ currentFlight WRITE setCurrentFlight NOTIFY currentFlightChanged)
    /// MediaLibrary's Filter: all, photos, videos.
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY currentFlightChanged)

    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    /// "6 selected · 18.2 MB"
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)

    /// The transfer row: {state: "" | "running" | "finished", title, name, percent, rate,
    /// fraction, detail, error}.
    Q_PROPERTY(QVariantMap transfer READ transfer NOTIFY transferChanged)
    /// Our downloads are queued or running (a preview's included).
    Q_PROPERTY(bool downloading READ downloading NOTIFY transferChanged)
    Q_PROPERTY(int deletesPending READ deletesPending NOTIFY transferChanged)
    /// A failed delete: "<file>: <why>"; cleared by the next action.
    Q_PROPERTY(QString notice READ notice NOTIFY transferChanged)
    /// Media stopped the live video for its downloads and starts it again when they end.
    Q_PROPERTY(bool videoPaused READ videoPaused NOTIFY videoPausedChanged)

    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY folderChanged)

    /// The file shown large: -1 none.
    Q_PROPERTY(int previewRow READ previewRow NOTIFY previewChanged)
    /// {name, timeText, sizeText, video, radiometric, thermal, onDevice}
    Q_PROPERTY(QVariantMap previewFile READ previewFile NOTIFY previewChanged)
    /// The picture once it is here (a local file).
    Q_PROPERTY(QUrl previewSource READ previewSource NOTIFY previewChanged)
    /// "loading", "waiting" (behind other downloads), "ready", "failed" or "tile" (a video or a
    /// radiometric TIFF: no picture).
    Q_PROPERTY(QString previewState READ previewState NOTIFY previewChanged)
    Q_PROPERTY(double previewProgress READ previewProgress NOTIFY previewChanged)

    // Development aids (--media-size, --media-demo).
    Q_PROPERTY(QSize startSize READ startSize CONSTANT)
    Q_PROPERTY(QString demo READ demo CONSTANT)

  public:
    MediaModel(QSettings& settings, QObject* parent);
    ~MediaModel() override;

    /// For the QML engine's image provider.
    std::shared_ptr<MediaThumbnails> thumbnails() const { return thumbnails_; }

    /// The session to work with; nullptr on disconnect, before the session goes.
    void setSession(PayloadSession* session);
    /// IMAGO_EVENT_MEDIA_RESULT
    void handleResult(uint32_t requestId, uint32_t op, int32_t result, const QString& text);

    void setStartSize(const QSize& size) { startSize_ = size; }
    void setDemo(const QString& demo) { demo_ = demo; }
    /// Downloads go here this run, without changing the setting.
    void overrideFolder(const QString& folder);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setStorage(int index);
    Q_INVOKABLE void requestThumbnail(const QString& name);

    // Selection, by row of the sheet
    Q_INVOKABLE void click(int row, bool toggle, bool range);
    /// Right click: a row outside the selection becomes the selection.
    Q_INVOKABLE void selectForMenu(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// The sheet row nearest to fraction of the current flight's timeline; -1 if none.
    Q_INVOKABLE int rowNearest(double fraction) const;

    // Downloads. pauseVideo: stop the live video until this batch ends.
    Q_INVOKABLE void downloadSelection(bool pauseVideo);
    Q_INVOKABLE void downloadFlight(int flightRow, bool pauseVideo);
    Q_INVOKABLE void downloadPreviewed(bool pauseVideo);
    Q_INVOKABLE void cancelDownloads();
    /// Hides the finished transfer row.
    Q_INVOKABLE void dismissTransfer();
    /// What a download of the selection, a flight or the preview would fetch (on this device
    /// already: skipped).
    Q_INVOKABLE int selectionToFetch() const;
    Q_INVOKABLE int flightToFetch(int flightRow) const;

    // Deletes
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void deletePreviewed();

    // Preview
    Q_INVOKABLE void openPreview(int row);
    Q_INVOKABLE void stepPreview(int step);
    Q_INVOKABLE void closePreview();
    /// Opens the previewed file from this device with the system's app (a video's player).
    Q_INVOKABLE void openPreviewedFile();

    // Download folder
    Q_INVOKABLE void setFolder(const QUrl& folder);
    Q_INVOKABLE void openFolder();

    bool isOpen() const { return open_; }
    void setOpen(bool open);
    bool connected() const { return session_ != nullptr; }
    int status() const { return noWebHost_ ? IMAGO_MEDIA_STATUS_FAILED : state_.status; }
    QString error() const { return error_; }
    int fileCount() const { return static_cast<int>(files_.size()); }
    QString storageText() const { return storageText_; }
    double storageFraction() const { return storageFraction_; }
    QStringList storages() const { return storages_; }
    int currentStorage() const { return state_.storage_current; }
    MediaFlightsModel* flights() { return &flightsModel_; }
    MediaFilesModel* files() { return &filesModel_; }
    int currentFlight() const { return currentFlight_; }
    void setCurrentFlight(int row);
    int filter() const { return filter_; }
    void setFilter(int filter);
    int selectionCount() const { return selection_.count(); }
    QString selectionText() const;
    QVariantMap transfer() const { return transfer_; }
    bool downloading() const { return !pending_.isEmpty(); }
    int deletesPending() const { return static_cast<int>(state_.deletes_pending); }
    QString notice() const { return notice_; }
    bool videoPaused() const { return videoPaused_; }
    QString folder() const { return folder_; }
    QUrl folderUrl() const { return QUrl::fromLocalFile(folder_); }
    int previewRow() const;
    QVariantMap previewFile() const;
    QUrl previewSource() const { return previewSource_; }
    QString previewState() const { return previewState_; }
    double previewProgress() const { return previewProgress_; }
    QSize startSize() const { return startSize_; }
    QString demo() const { return demo_; }

  signals:
    void openChanged();
    void stateChanged();
    void currentFlightChanged();
    void selectionChanged();
    void transferChanged();
    void videoPausedChanged();
    void folderChanged();
    void previewChanged();
    /// The list arrived with files, the first time since Media opened.
    void filesReady();

  private:
    struct Pending {
        QString name;
        QString path;
        qint64 size = -1;
        bool preview = false;
    };
    struct Request {
        QString name;
        QString path;
        qint64 size = -1;
    };

    void poll();
    void reload();
    void rebuildRows();
    void updateSelectionRows();
    void updateTransfer();
    QStringList visibleNames() const;
    const Media::Flight* flightOf(const Media::File& file) const;
    const Media::File* fileNamed(const QString& name) const;
    QString pathFor(const Media::File& file) const;
    bool onDevice(const Media::File& file) const;
    QString cachePath(const QString& name) const;
    QList<Request> toFetch(const QStringList& names) const;
    void startDownloads(const QList<Request>& requests, bool pauseVideo);
    void batchEnded();
    void resumeVideo();
    void showPreview(const QString& name);
    void startPreviewDownload();
    void setNotice(const QString& notice);
    void reset();

    QSettings& settings_;
    PayloadSession* session_ = nullptr;
    std::shared_ptr<MediaThumbnails> thumbnails_;
    QTimer pollTimer_;
    QTimer retryTimer_;
    bool open_ = false;
    bool announcedFiles_ = false;

    imago_media_state state_{};
    uint32_t revision_ = 0;
    /// The profile has no payload IP, so there is no web server to ask.
    bool noWebHost_ = false;
    QString error_;
    QString storageText_;
    double storageFraction_ = -1.0;
    QStringList storages_;
    std::vector<Media::File> files_;
    std::vector<Media::Flight> flights_;
    QHash<QString, int> fileIndex_;
    /// Whether a download path exists, asked once per path (until a download or a new folder).
    mutable QHash<QString, bool> exists_;

    MediaFilesModel filesModel_;
    MediaFlightsModel flightsModel_;
    int currentFlight_ = 0;
    /// The current flight across reloads, which regroup flights while capture times arrive:
    /// the flight that holds this file.
    QString currentFlightFile_;
    bool followLatest_ = true;
    int filter_ = Media::AllFiles;
    Media::Selection selection_;

    QHash<uint32_t, Pending> pending_;
    Media::Batch batch_;
    bool batchShown_ = false;
    QVariantMap transfer_;
    QString notice_;
    bool videoPaused_ = false;

    QString folder_;
    QString previewName_;
    QUrl previewSource_;
    QString previewState_;
    double previewProgress_ = 0.0;
    uint32_t previewDownloadId_ = 0;
    QString previewDownloadName_;

    QSize startSize_;
    QString demo_;
};
