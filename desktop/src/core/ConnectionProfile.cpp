#include "core/ConnectionProfile.h"

#include <imago/imago_core.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace {

const char* kProfilesKey = "connectionProfiles.v1";
const char* kSelectedKey = "connectionProfiles.selected";

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

constexpr EnumName<ConnectionProfile::MavlinkMode> kMavlinkModes[] = {
    {ConnectionProfile::MavlinkMode::Direct, "direct"},
    {ConnectionProfile::MavlinkMode::Listen, "listen"},
};
constexpr EnumName<ConnectionProfile::VideoTransport> kTransports[] = {
    {ConnectionProfile::VideoTransport::Auto, "auto"},
    {ConnectionProfile::VideoTransport::Udp, "udp"},
    {ConnectionProfile::VideoTransport::Tcp, "tcp"},
};
constexpr EnumName<ConnectionProfile::GimbalProtocol> kGimbalProtocols[] = {
    {ConnectionProfile::GimbalProtocol::Attitude, "attitude"},
    {ConnectionProfile::GimbalProtocol::MountControl, "mountControl"},
    {ConnectionProfile::GimbalProtocol::Manager, "manager"},
};

template <typename Enum, size_t N>
QString nameOf(const EnumName<Enum> (&names)[N], Enum value) {
    for (const auto& entry : names) {
        if (entry.value == value) {
            return QString::fromLatin1(entry.name);
        }
    }
    return QString::fromLatin1(names[0].name);
}

/// Unknown names keep the default, like coerceInputValues on Android.
template <typename Enum, size_t N>
Enum valueOf(const EnumName<Enum> (&names)[N], const QVariant& name, Enum fallback) {
    const QString text = name.toString();
    for (const auto& entry : names) {
        if (text == QLatin1String(entry.name)) {
            return entry.value;
        }
    }
    return fallback;
}

int intOr(const QVariant& value, int fallback) {
    bool ok = false;
    const int result = value.toInt(&ok);
    return ok ? result : fallback;
}

} // namespace

const QList<int> ConnectionProfile::videoLagChoices = {0, 300, 500, 1000};
const QList<int> ConnectionProfile::smoothingChoices = {0, 50, 100, 200};

ConnectionProfile ConnectionProfile::make(const QString& name) {
    ConnectionProfile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    profile.name = name;
    return profile;
}

ConnectionProfile ConnectionProfile::herelink() {
    ConnectionProfile p = make(QStringLiteral("Orus L · Herelink"));
    p.payloadIP = QStringLiteral("192.168.144.240");
    p.videoURL = QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream");
    return p;
}

ConnectionProfile ConnectionProfile::direct() {
    ConnectionProfile p = make(QStringLiteral("Orus L · Direct"));
    p.payloadIP = QStringLiteral("192.168.55.1");
    return p;
}

ConnectionProfile ConnectionProfile::simulator() {
    ConnectionProfile p = make(QStringLiteral("orus-sim on this computer"));
    p.payloadIP = QStringLiteral("127.0.0.1");
    p.localPort = 14567;
    return p;
}

bool ConnectionProfile::isIPv4(const QString& text) {
    const QStringList parts = text.trimmed().split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return false;
    }
    for (const QString& part : parts) {
        if (part.isEmpty() || part.size() > 3) {
            return false;
        }
        for (const QChar c : part) {
            if (!c.isDigit()) {
                return false;
            }
        }
        if (part.toInt() > 255) {
            return false;
        }
    }
    return true;
}

QString ConnectionProfile::validationError() const {
    if (name.trimmed().isEmpty()) {
        return QStringLiteral("Enter a name.");
    }
    if (mavlinkMode == MavlinkMode::Direct && !isIPv4(payloadIP)) {
        return QStringLiteral("Enter the payload IP address, e.g. 192.168.144.240.");
    }
    const QString url = videoURL.trimmed();
    if (!url.isEmpty() && !url.toLower().startsWith(QLatin1String("rtsp://"))) {
        return QStringLiteral("The video URL must start with rtsp://");
    }
    if (mavlinkMode == MavlinkMode::Listen && url.isEmpty()) {
        return QStringLiteral("Listen mode needs a video URL.");
    }
    if (mavlinkPort < 1 || mavlinkPort > 65535 || localPort < 0 || localPort > 65535) {
        return QStringLiteral("Ports go from 1 to 65535.");
    }
    return {};
}

int ConnectionProfile::transportValue() const {
    switch (videoTransport) {
    case VideoTransport::Udp:
        return IMAGO_RTSP_UDP;
    case VideoTransport::Tcp:
        return IMAGO_RTSP_TCP;
    case VideoTransport::Auto:
        break;
    }
    return IMAGO_RTSP_AUTO;
}

int ConnectionProfile::gimbalProtocolValue() const {
    switch (gimbalProtocol) {
    case GimbalProtocol::MountControl:
        return IMAGO_GIMBAL_PROTOCOL_V1;
    case GimbalProtocol::Manager:
        return IMAGO_GIMBAL_PROTOCOL_MANAGER;
    case GimbalProtocol::Attitude:
        break;
    }
    return IMAGO_GIMBAL_PROTOCOL_V2;
}

QVariantMap ConnectionProfile::toVariant() const {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("mavlinkMode"), nameOf(kMavlinkModes, mavlinkMode)},
        {QStringLiteral("payloadIP"), payloadIP},
        {QStringLiteral("mavlinkPort"), mavlinkPort},
        {QStringLiteral("localPort"), localPort},
        {QStringLiteral("videoURL"), videoURL},
        {QStringLiteral("videoTransport"), nameOf(kTransports, videoTransport)},
        {QStringLiteral("gimbalProtocol"), nameOf(kGimbalProtocols, gimbalProtocol)},
        {QStringLiteral("setStandardRCMode"), setStandardRCMode},
        {QStringLiteral("maxVideoLagMs"), maxVideoLagMs},
        {QStringLiteral("keepWifiAwake"), keepWifiAwake},
        {QStringLiteral("videoSmoothingMs"), videoSmoothingMs},
    };
}

ConnectionProfile ConnectionProfile::fromVariant(const QVariantMap& map) {
    ConnectionProfile p;
    p.id = map.value(QStringLiteral("id")).toString();
    if (p.id.isEmpty()) {
        p.id = make({}).id;
    }
    p.name = map.value(QStringLiteral("name"), p.name).toString();
    p.mavlinkMode = valueOf(kMavlinkModes, map.value(QStringLiteral("mavlinkMode")), p.mavlinkMode);
    p.payloadIP = map.value(QStringLiteral("payloadIP")).toString();
    p.mavlinkPort = intOr(map.value(QStringLiteral("mavlinkPort")), p.mavlinkPort);
    p.localPort = intOr(map.value(QStringLiteral("localPort")), p.localPort);
    p.videoURL = map.value(QStringLiteral("videoURL")).toString();
    p.videoTransport = valueOf(kTransports, map.value(QStringLiteral("videoTransport")), p.videoTransport);
    p.gimbalProtocol =
        valueOf(kGimbalProtocols, map.value(QStringLiteral("gimbalProtocol")), p.gimbalProtocol);
    p.setStandardRCMode = map.value(QStringLiteral("setStandardRCMode"), p.setStandardRCMode).toBool();
    p.maxVideoLagMs = intOr(map.value(QStringLiteral("maxVideoLagMs")), p.maxVideoLagMs);
    // The first version stored "wifiKeepAwake", which stalled the Herelink: ignored on purpose.
    p.keepWifiAwake = map.value(QStringLiteral("keepWifiAwake"), p.keepWifiAwake).toBool();
    p.videoSmoothingMs = intOr(map.value(QStringLiteral("videoSmoothingMs")), p.videoSmoothingMs);
    return p;
}

QJsonObject ConnectionProfile::toJson() const { return QJsonObject::fromVariantMap(toVariant()); }

ConnectionProfile ConnectionProfile::fromJson(const QJsonObject& json) {
    return fromVariant(json.toVariantMap());
}

// ----------------------------------------------------------------------------------------------

QList<ConnectionProfile> ProfileStore::defaults() {
    return {ConnectionProfile::herelink(), ConnectionProfile::direct(), ConnectionProfile::simulator()};
}

QList<ConnectionProfile> ProfileStore::load() const {
    const QByteArray json = settings_.value(QLatin1String(kProfilesKey)).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QList<ConnectionProfile> profiles;
    for (const QJsonValue& value : doc.array()) {
        if (value.isObject()) {
            profiles.append(ConnectionProfile::fromJson(value.toObject()));
        }
    }
    return profiles.isEmpty() ? defaults() : profiles;
}

void ProfileStore::save(const QList<ConnectionProfile>& profiles) {
    QJsonArray array;
    for (const ConnectionProfile& profile : profiles) {
        array.append(profile.toJson());
    }
    settings_.setValue(QLatin1String(kProfilesKey),
                       QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

QString ProfileStore::selectedId() const { return settings_.value(QLatin1String(kSelectedKey)).toString(); }

void ProfileStore::setSelectedId(const QString& id) {
    if (id.isEmpty()) {
        settings_.remove(QLatin1String(kSelectedKey));
    } else {
        settings_.setValue(QLatin1String(kSelectedKey), id);
    }
}
