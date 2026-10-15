#pragma once

#include <QJsonObject>
#include <QList>
#include <QSettings>
#include <QString>
#include <QVariantMap>

/// How the app reaches one payload. Like Payload Plus, control and video are configured
/// separately: MAVLink goes to the Orus L, video usually comes from the datalink (Herelink).
///
/// Stored as the same JSON as on iOS and Android, so a profile reads the same everywhere.
struct ConnectionProfile {
    enum class MavlinkMode { Direct, Listen };
    enum class VideoTransport { Auto, Udp, Tcp };
    enum class GimbalProtocol { Attitude, MountControl, Manager };

    QString id;
    QString name = QStringLiteral("Payload");
    /// Direct: send to the payload's IP. Listen: answer whoever sends MAVLink (Herelink forwarding).
    MavlinkMode mavlinkMode = MavlinkMode::Direct;
    QString payloadIP;
    int mavlinkPort = 14566;
    /// 0 = default (same as mavlinkPort in direct mode, 14550 in listen mode).
    int localPort = 0;
    /// Empty = use the URL the payload announces over MAVLink.
    QString videoURL;
    VideoTransport videoTransport = VideoTransport::Auto;
    GimbalProtocol gimbalProtocol = GimbalProtocol::Attitude;
    /// Sends RC_MODE = standard when the link comes up. Off until confirmed on the firmware.
    bool setStandardRCMode = false;
    /// Restart the video when it stays this far behind live (a queue upstream). 0 = never.
    int maxVideoLagMs = 500;
    /// A 1-byte datagram ~30 times a second to the video host's discard port, so a Wi-Fi
    /// adapter does not doze between video packets. Off by default (see imago_config).
    bool keepWifiAwake = false;
    /// Playout smoothing in ms: frames that arrive in bursts are shown at the stream's own
    /// pace, adding up to this much delay. 0 = every frame at once (lowest latency).
    int videoSmoothingMs = 0;

    /// A new profile with a fresh ID.
    static ConnectionProfile make(const QString& name);

    /// Problem with the profile, or an empty string if it can be used.
    QString validationError() const;

    QJsonObject toJson() const;
    /// Missing or unknown fields keep their defaults, as in the mobile apps.
    static ConnectionProfile fromJson(const QJsonObject& json);

    /// The same fields for QML, which edits a copy and hands it back.
    QVariantMap toVariant() const;
    static ConnectionProfile fromVariant(const QVariantMap& map);

    /// The core's enum values.
    int transportValue() const;
    int gimbalProtocolValue() const;

    bool operator==(const ConnectionProfile&) const = default;

    static const QList<int> videoLagChoices;
    static const QList<int> smoothingChoices;

    /// Field setup confirmed with the user: Orus L behind Herelink, video from Herelink.
    static ConnectionProfile herelink();
    /// Orus L on a direct Ethernet link, video announced by the payload.
    static ConnectionProfile direct();
    /// tools/orus-sim on this computer. The local port differs from the simulator's 14566
    /// because both run on the same machine.
    static ConnectionProfile simulator();

    static bool isIPv4(const QString& text);
};

/// Saved profiles and the selection, in the app's settings file.
class ProfileStore {
  public:
    explicit ProfileStore(QSettings& settings) : settings_(settings) {}

    QList<ConnectionProfile> load() const;
    void save(const QList<ConnectionProfile>& profiles);

    QString selectedId() const;
    void setSelectedId(const QString& id);

    static QList<ConnectionProfile> defaults();

  private:
    QSettings& settings_;
};
