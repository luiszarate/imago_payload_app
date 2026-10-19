#include "core/MarkedPoints.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {

QString isoTime(const QDateTime& time) { return time.toUTC().toString(Qt::ISODate); }

} // namespace

QString TargetPosition::coordinateText() const {
    return QString::asprintf("%.5f, %.5f", latitude, longitude);
}

QUrl TargetPosition::mapsUrl() const {
    return QUrl(QString::asprintf("https://www.google.com/maps/search/?api=1&query=%.5f,%.5f", latitude, longitude));
}

TargetPosition MarkedPoints::rounded(double latitude, double longitude, float altitude) {
    return {std::round(latitude * 1e5) / 1e5, std::round(longitude * 1e5) / 1e5, std::round(altitude)};
}

MarkedPoint MarkedPoints::make(const QList<MarkedPoint>& existing, const TargetPosition& position,
                               const QDateTime& time) {
    int highest = 0;
    for (const MarkedPoint& point : existing) {
        highest = std::max(highest, point.name.mid(6).toInt());
    }
    return {QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper(),
            QStringLiteral("Point %1").arg(highest + 1), time, position};
}

QByteArray MarkedPoints::toJson(const QList<MarkedPoint>& points) {
    QJsonArray array;
    for (const MarkedPoint& point : points) {
        array.append(QJsonObject{
            {QStringLiteral("id"), point.id},
            {QStringLiteral("name"), point.name},
            {QStringLiteral("time"), isoTime(point.time)},
            {QStringLiteral("position"), QJsonObject{{QStringLiteral("latitude"), point.position.latitude},
                                                     {QStringLiteral("longitude"), point.position.longitude},
                                                     {QStringLiteral("altitude"), point.position.altitude}}},
        });
    }
    return QJsonDocument(array).toJson(QJsonDocument::Indented);
}

QList<MarkedPoint> MarkedPoints::fromJson(const QByteArray& json) {
    QList<MarkedPoint> points;
    for (const QJsonValue& value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject object = value.toObject();
        const QJsonObject position = object.value(QStringLiteral("position")).toObject();
        MarkedPoint point;
        point.id = object.value(QStringLiteral("id")).toString();
        point.name = object.value(QStringLiteral("name")).toString();
        point.time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(), Qt::ISODate);
        point.position = {position.value(QStringLiteral("latitude")).toDouble(),
                          position.value(QStringLiteral("longitude")).toDouble(),
                          static_cast<float>(position.value(QStringLiteral("altitude")).toDouble())};
        if (point.id.isEmpty() || !point.time.isValid()) {
            continue;
        }
        points.append(point);
    }
    return points;
}

QString MarkedPoints::toCsv(const QList<MarkedPoint>& points) {
    QString csv = QStringLiteral("name,time_utc,latitude,longitude,altitude_m\n");
    for (const MarkedPoint& point : points) {
        QString name = point.name;
        name.replace(QLatin1Char(','), QLatin1Char(' '));
        csv += QStringLiteral("%1,%2,").arg(name, isoTime(point.time)) +
               QString::asprintf("%.5f,%.5f,%.0f\n", point.position.latitude, point.position.longitude,
                                 static_cast<double>(point.position.altitude));
    }
    return csv;
}

QString MarkedPoints::toKml(const QList<MarkedPoint>& points) {
    QString placemarks;
    for (const MarkedPoint& point : points) {
        QString name = point.name;
        name.replace(QLatin1Char('&'), QStringLiteral("&amp;")).replace(QLatin1Char('<'), QStringLiteral("&lt;"));
        placemarks += QStringLiteral("  <Placemark><name>%1</name><TimeStamp><when>%2</when></TimeStamp>")
                          .arg(name, isoTime(point.time)) +
                      QString::asprintf("<Point><coordinates>%.5f,%.5f,%.0f</coordinates></Point></Placemark>\n",
                                        point.position.longitude, point.position.latitude,
                                        static_cast<double>(point.position.altitude));
    }
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><name>Marked points</name>\n") +
           placemarks + QStringLiteral("</Document></kml>\n");
}
