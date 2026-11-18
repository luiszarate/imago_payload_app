#pragma once

#include "core/MapTiles.h"

#include <imago/imago_core.h>

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QNetworkReply;

/// The small map over the picture (App.map), as on the Quest: north up, centred on the
/// aircraft, drawn from tiles kept on this computer (`tiles/<style>/<z>/<x>/<y>` in the app's
/// data folder). The payload's Wi-Fi has no internet, so what is on disk is what there is:
/// Settings › Map downloads a flight area beforehand, and a tile that is missing in flight is
/// asked for at most once a minute, in case this computer does have a way out.
///
/// Positions go to QML as tile coordinates at the map's zoom (column, row with their
/// fractions), so the view only places tiles and marks; decoding is QML's asynchronous Image.
class MapModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.map")

    // Settings, remembered across launches.
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadDetail READ downloadDetail WRITE setDownloadDetail NOTIFY settingsChanged)
    Q_PROPERTY(bool satellite READ satellite NOTIFY settingsChanged)
    Q_PROPERTY(int zoom READ zoom NOTIFY settingsChanged)
    Q_PROPERTY(int minZoom READ minZoom CONSTANT)
    Q_PROPERTY(int maxZoom READ maxZoom CONSTANT)
    /// Who the tiles shown are by: drawn on the map.
    Q_PROPERTY(QString credit READ credit NOTIFY settingsChanged)
    /// {streetUrl, streetCredit, satelliteUrl, satelliteCredit}
    Q_PROPERTY(QVariantMap sources READ sources NOTIFY settingsChanged)

    // The aircraft and the camera's target, in tile coordinates at `zoom`.
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY positionChanged)
    Q_PROPERTY(double centreX READ centreX NOTIFY viewChanged)
    Q_PROPERTY(double centreY READ centreY NOTIFY viewChanged)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY viewChanged)
    Q_PROPERTY(double targetX READ targetX NOTIFY viewChanged)
    Q_PROPERTY(double targetY READ targetY NOTIFY viewChanged)
    /// Bumped when a tile arrives in flight, so the view asks for its sources again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

    // Offline area (Settings › Map)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadChanged)
    /// "On this computer: satellite … · street …"; empty while counted.
    Q_PROPERTY(QString storedText READ storedText NOTIFY storedChanged)
    /// Tiles are being counted or deleted.
    Q_PROPERTY(bool storeBusy READ storeBusy NOTIFY storedChanged)

  public:
    static constexpr qint64 kRetryMs = 60'000;   // a missing tile is asked for again after this
    static constexpr qint64 kOfflineMs = 60'000; // after a failed fetch nothing is fetched for this long
    static constexpr int kGiveUpAfter = 8;       // failures in a row that end a download
    static constexpr int kPoliteMs = 120;        // between the tiles of a download
    static constexpr int kMaxFlightFetches = 2;  // missing tiles asked for at the same time

    MapModel(QSettings& settings, const QString& appVersion, QObject* parent);
    ~MapModel() override;

    // Map buttons
    Q_INVOKABLE void zoomBy(int step);
    Q_INVOKABLE void toggleStyle();
    /// The file URL of a tile at the map's zoom and style, or an empty string when it is not
    /// stored (then asked for in the background, at most once a minute).
    Q_INVOKABLE QString tileSource(int column, int row);

    // Settings › Map
    /// {lat, lon, radiusKm, known}: the area to download, the aircraft's last position when known.
    Q_INVOKABLE QVariantMap area() const;
    /// Starts downloading a style around a point; the problem with the fields, or empty.
    Q_INVOKABLE QString download(const QString& style, const QString& lat, const QString& lon,
                                 const QString& radiusKm);
    Q_INVOKABLE void stopDownload();
    Q_INVOKABLE void deleteTiles(const QString& style);
    Q_INVOKABLE void refreshStored();
    /// Keeps the tile sources; the problem with a URL instead (nothing kept), or empty.
    Q_INVOKABLE QString setSources(const QString& streetUrl, const QString& streetCredit,
                                   const QString& satelliteUrl, const QString& satelliteCredit);
    Q_INVOKABLE void restoreSources();

    /// From each poll of the payload's state.
    void setAircraft(const imago_state& s);
    /// Keeps the aircraft's last position for the next download (at disconnect).
    void rememberPosition();
    /// A session ended: no aircraft or target until the next one reports them.
    void clearAircraft();

    bool visible() const { return settings_.visible; }
    void setVisible(bool on);
    bool downloadDetail() const { return settings_.downloadDetail; }
    void setDownloadDetail(bool on);
    bool satellite() const { return settings_.satellite(); }
    int zoom() const { return settings_.zoom; }
    int minZoom() const { return TileMath::kMinZoom; }
    int maxZoom() const { return TileMath::kMaxZoom; }
    QString credit() const { return settings_.credit(); }
    QVariantMap sources() const;
    bool hasPosition() const { return hasPosition_; }
    double centreX() const { return TileMath::x(lon_, settings_.zoom); }
    double centreY() const { return TileMath::y(lat_, settings_.zoom); }
    bool hasTarget() const { return hasTarget_; }
    double targetX() const { return TileMath::x(targetLon_, settings_.zoom); }
    double targetY() const { return TileMath::y(targetLat_, settings_.zoom); }
    int revision() const { return revision_; }
    bool downloading() const { return download_.active; }
    QString downloadStatus() const { return downloadStatus_; }
    QString storedText() const { return storedText_; }
    bool storeBusy() const { return storeJobs_ > 0; }

  signals:
    void settingsChanged();
    void viewChanged();
    /// After viewChanged, so what shows with the position already has its coordinates.
    void positionChanged();
    void revisionChanged();
    void downloadChanged();
    void storedChanged();

  private:
    struct Download {
        bool active = false;
        bool cancelled = false;
        QString style;
        QList<TileId> tiles;
        qsizetype next = 0;
        int done = 0;
        int failed = 0;
        int failedInARow = 0;
        double lat = 0.0;
        double lon = 0.0;
        int topZoom = TileMath::kDownloadMaxZoom;
    };

    QString root() const;
    QString filePath(const QString& style, const TileId& id) const;
    bool isStored(const QString& style, const TileId& id) const;
    QNetworkReply* get(const QString& style, const TileId& id);
    /// Writes a fetched tile; false if the reply had none (an error, or a body that is no
    /// picture, such as a captive portal's page).
    bool store(QNetworkReply* reply, const QString& style, const TileId& id);
    void fetchInFlight(const QString& style, const TileId& id);
    void downloadStep();
    void downloadFinished();
    void updateDownloadStatus();
    void saveSettings();
    void runOnStore(std::function<void()> work);

    QSettings& store_;
    MapSettings settings_;
    QString userAgent_;
    QNetworkAccessManager network_;
    /// Counting and deleting tiles, off the main thread.
    QThreadPool pool_;
    int storeJobs_ = 0;
    QString storedText_;

    bool hasPosition_ = false;
    double lat_ = 0.0;
    double lon_ = 0.0;
    bool hasTarget_ = false;
    double targetLat_ = 0.0;
    double targetLon_ = 0.0;
    int revision_ = 0;

    QElapsedTimer clock_;
    /// Tile key -> when it may be asked for again.
    QHash<QString, qint64> missing_;
    QSet<QString> pending_;
    qint64 offlineUntilMs_ = 0;

    Download download_;
    QPointer<QNetworkReply> downloadReply_;
    QString downloadStatus_;
};
