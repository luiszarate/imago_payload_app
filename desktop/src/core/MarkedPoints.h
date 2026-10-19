#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

/// Where the camera points, as the payload works it out (its OSD's TARGET line: laser range,
/// gimbal attitude and the GPS position it gets from the autopilot), as in the iOS app
/// (PayloadStatus.swift, AppModel+Target.swift).
struct TargetPosition {
    double latitude = 0.0;
    double longitude = 0.0;
    /// As the payload reports it (its OSD's ALT), metres.
    float altitude = 0.0f;

    /// "22.16984, -101.05302": the OSD's precision, about 1 m (the payload sends 32-bit floats).
    QString coordinateText() const;
    /// A map link any browser opens (the desktop has no system Maps app).
    QUrl mapsUrl() const;
    bool operator==(const TargetPosition&) const = default;
};

/// A target position the operator kept, with when it was marked.
struct MarkedPoint {
    QString id;
    QString name;
    QDateTime time;
    TargetPosition position;
    bool operator==(const MarkedPoint&) const = default;
};

namespace MarkedPoints {

/// Rounded to the payload's precision, so the screen only redraws when the point moves.
TargetPosition rounded(double latitude, double longitude, float altitude);

/// "Point N", one past the highest number used so far.
MarkedPoint make(const QList<MarkedPoint>& existing, const TargetPosition& position, const QDateTime& time);

/// The same JSON as the iOS app's Documents/marked-points.json, so a list can move between them.
QByteArray toJson(const QList<MarkedPoint>& points);
QList<MarkedPoint> fromJson(const QByteArray& json);

/// CSV for spreadsheets and KML for Google Earth and most mapping apps.
QString toCsv(const QList<MarkedPoint>& points);
QString toKml(const QList<MarkedPoint>& points);

} // namespace MarkedPoints
