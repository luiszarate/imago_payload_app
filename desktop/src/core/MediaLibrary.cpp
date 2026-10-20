#include "core/MediaLibrary.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace Media {

namespace {

QDateTime local(qint64 utc) { return QDateTime::fromSecsSinceEpoch(utc).toLocalTime(); }

QLocale english() { return QLocale(QLocale::English, QLocale::UnitedStates); }

} // namespace

File File::from(const imago_media_file& file) {
    File f;
    f.name = QString::fromUtf8(file.name);
    f.kind = file.kind;
    f.thermal = file.thermal;
    f.size = file.size_bytes;
    f.time = file.capture_time;
    f.flight = static_cast<int>(file.flight);
    f.thumbnail = file.thumbnail;
    f.thumbnailFailed = file.thumbnail_failed;
    return f;
}

Flight Flight::from(const imago_media_flight& flight) {
    Flight f;
    f.start = flight.start_time;
    f.end = flight.end_time;
    f.firstFile = static_cast<int>(flight.first_file);
    f.fileCount = static_cast<int>(flight.file_count);
    f.photos = static_cast<int>(flight.photos);
    f.videos = static_cast<int>(flight.videos);
    f.size = flight.size_bytes;
    return f;
}

bool matches(const File& file, int filter) {
    switch (filter) {
    case PhotosOnly:
        return file.kind == IMAGO_MEDIA_PHOTO || file.kind == IMAGO_MEDIA_RADIOMETRIC;
    case VideosOnly:
        return file.kind == IMAGO_MEDIA_VIDEO;
    default:
        return true;
    }
}

QList<int> sheetOrder(const std::vector<File>& files, const Flight& flight) {
    QList<int> order;
    const int end = std::min<int>(flight.firstFile + flight.fileCount, static_cast<int>(files.size()));
    for (int i = std::max(flight.firstFile, 0); i < end; ++i) {
        order << i;
    }
    std::stable_sort(order.begin(), order.end(), [&files](int a, int b) {
        const qint64 ta = files[a].time;
        const qint64 tb = files[b].time;
        if ((ta < 0) != (tb < 0)) return tb < 0; // timed ones first
        return ta < tb;
    });
    return order;
}

QString sizeText(qint64 bytes) {
    if (bytes < 0) {
        return {};
    }
    constexpr double kMiB = 1024.0 * 1024.0;
    const double mib = static_cast<double>(bytes) / kMiB;
    // One decimal, never "0.0 MB" for a file that has bytes.
    if (mib < 1000.0) {
        return QStringLiteral("%1 MB").arg(bytes > 0 ? std::max(mib, 0.1) : 0.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(mib / 1024.0, 0, 'f', 1);
}

QString storageText(double usedMiB, double totalMiB) {
    return QStringLiteral("%1 of %2 GB").arg(usedMiB / 1024.0, 0, 'f', 1).arg(totalMiB / 1024.0, 0, 'f', 1);
}

QString captureTimeText(qint64 utc) {
    return utc < 0 ? QString() : local(utc).toString(QStringLiteral("HH:mm:ss"));
}

QString flightDateText(const Flight& flight) {
    if (!flight.hasTime()) {
        return QStringLiteral("No capture time");
    }
    return english().toString(local(flight.start), QStringLiteral("MMM d"));
}

QString flightTimeText(const Flight& flight) {
    if (!flight.hasTime()) {
        return {};
    }
    return local(flight.start).toString(QStringLiteral("HH:mm")) + QStringLiteral(" - ") +
           local(std::max(flight.end, flight.start)).toString(QStringLiteral("HH:mm"));
}

QString countsText(int photos, int videos) {
    QStringList parts;
    if (photos > 0 || videos == 0) {
        parts << (photos == 1 ? QStringLiteral("1 photo") : QStringLiteral("%1 photos").arg(photos));
    }
    if (videos > 0) {
        parts << (videos == 1 ? QStringLiteral("1 video") : QStringLiteral("%1 videos").arg(videos));
    }
    return parts.join(QStringLiteral(" · "));
}

QString flightFolderName(const Flight& flight) {
    return flight.hasTime() ? local(flight.start).toString(QStringLiteral("yyyy-MM-dd HH-mm"))
                            : QStringLiteral("No capture time");
}

QString downloadPath(const QString& root, const Flight& flight, const QString& name) {
    // A name from the payload never climbs out of its folder.
    QString safe = name;
    safe.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (safe == QLatin1String("..") || safe == QLatin1String(".")) {
        safe = QStringLiteral("_");
    }
    return QDir(root).filePath(flightFolderName(flight) + QLatin1Char('/') + safe);
}

double timelineFraction(qint64 time, const Flight& flight) {
    if (time < 0 || !flight.hasTime()) {
        return -1.0;
    }
    const qint64 span = flight.end - flight.start;
    if (span <= 0) {
        return 0.5;
    }
    return std::clamp(static_cast<double>(time - flight.start) / static_cast<double>(span), 0.0, 1.0);
}

int nearestIndex(const QList<qint64>& times, const Flight& flight, double fraction) {
    int best = -1;
    double bestDistance = 0.0;
    for (int i = 0; i < times.size(); ++i) {
        const double at = timelineFraction(times[i], flight);
        const double distance = at < 0 ? 2.0 : std::fabs(at - fraction);
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

// MARK: Selection

void Selection::click(const QStringList& names, int index, bool toggle, bool range) {
    if (index < 0 || index >= names.size()) {
        return;
    }
    const QString& name = names[index];
    const qsizetype anchorIndex = anchor_.isEmpty() ? -1 : names.indexOf(anchor_);
    if (range && anchorIndex >= 0) {
        if (!toggle) {
            selected_.clear();
        }
        const int anchor = static_cast<int>(anchorIndex);
        const int from = std::min(anchor, index);
        const int to = std::max(anchor, index);
        for (int i = from; i <= to; ++i) {
            selected_.insert(names[i]);
        }
        return; // the anchor stays, so the next Shift-click measures from the same place
    }
    if (toggle) {
        if (!selected_.remove(name)) {
            selected_.insert(name);
        }
    } else {
        selected_ = {name};
    }
    anchor_ = name;
}

void Selection::selectAll(const QStringList& names) {
    selected_ = QSet<QString>(names.begin(), names.end());
    if (anchor_.isEmpty() && !names.isEmpty()) {
        anchor_ = names.first();
    }
}

void Selection::clear() {
    selected_.clear();
    anchor_.clear();
}

void Selection::retain(const QStringList& names) {
    const QSet<QString> present(names.begin(), names.end());
    selected_.intersect(present);
    if (!present.contains(anchor_)) {
        anchor_.clear();
    }
}

QStringList Selection::ordered(const QStringList& names) const {
    QStringList result;
    for (const QString& name : names) {
        if (selected_.contains(name)) result << name;
    }
    return result;
}

// MARK: Batch

double Batch::fraction(qint64 currentBytes) const {
    if (total <= 0) {
        return 0.0;
    }
    if (totalBytes > 0) {
        return std::clamp(static_cast<double>(finishedBytes + std::max<qint64>(currentBytes, 0)) /
                              static_cast<double>(totalBytes),
                          0.0, 1.0);
    }
    return static_cast<double>(done + failed + cancelled) / total;
}

QString Batch::runningTitle() const {
    return QStringLiteral("Downloading %1 of %2").arg(std::min(done + failed + cancelled + 1, total)).arg(total);
}

QString Batch::finishedTitle() const {
    return done == 0 ? QStringLiteral("Nothing downloaded") : QStringLiteral("%1 downloaded").arg(done);
}

QString Batch::finishedDetail() const {
    QStringList parts;
    if (failed > 0) parts << QStringLiteral("%1 failed").arg(failed);
    if (cancelled > 0) parts << QStringLiteral("%1 cancelled").arg(cancelled);
    return parts.join(QStringLiteral(" · "));
}

QString rateText(double bytesPerSecond) {
    return QStringLiteral("%1 MB/s").arg(std::max(bytesPerSecond, 0.0) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace Media
