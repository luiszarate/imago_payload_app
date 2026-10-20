#pragma once

#include <imago/imago_core.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

/// The Media window's logic without Qt Quick: the files and flights as the core lists them, the
/// texts the window shows, where downloads go, the click selection and a download batch.
namespace Media {

struct File {
    QString name;
    int kind = IMAGO_MEDIA_OTHER;
    bool thermal = false;
    qint64 size = -1; ///< bytes, -1 unknown
    qint64 time = -1; ///< UTC seconds, -1 unknown
    int flight = 0;
    bool thumbnail = false;
    bool thumbnailFailed = false;
    int camera = IMAGO_MEDIA_CAMERA_UNKNOWN;
    /// The Orus L makes two files per shot, one per camera (IMG_EO_205.jpeg + IMG_IR_205.jpeg,
    /// field report 2026-10-20): the other file's name, empty if none.
    QString pair;
    /// Stands for its shot: the EO file of a pair, or a file without a pair. The sheet shows
    /// these only and acts on both files.
    bool primary = true;
    /// The thumbnail is the IR twin's picture (EO photos carry none and are ~10 MB).
    bool thumbnailFromPair = false;

    static File from(const imago_media_file& file);
    bool isVideo() const { return kind == IMAGO_MEDIA_VIDEO; }
    /// A picture Qt can show large (JPEG); the radiometric TIFF is not one.
    bool isPhoto() const { return kind == IMAGO_MEDIA_PHOTO; }
    bool hasPair() const { return !pair.isEmpty(); }
    /// The shot's files: this one, then its pair.
    QStringList shotNames() const { return hasPair() ? QStringList{name, pair} : QStringList{name}; }
    bool operator==(const File&) const = default;
};

struct Flight {
    qint64 start = -1; ///< UTC seconds; -1: the files without a capture time
    qint64 end = -1;
    int firstFile = 0;
    int fileCount = 0;
    int photos = 0;
    int videos = 0;
    qint64 size = 0;

    static Flight from(const imago_media_flight& flight);
    bool hasTime() const { return start >= 0; }
    bool operator==(const Flight&) const = default;
};

enum Filter { AllFiles = 0, PhotosOnly = 1, VideosOnly = 2 };
bool matches(const File& file, int filter);

/// The flight's shots (their primary files) as the sheet shows them: in capture order, oldest
/// first, so the sheet reads left to right like the flight's timeline strip (the core lists
/// them newest first). Files without a time keep the core's order, after the others. Indices
/// into files.
QList<int> sheetOrder(const std::vector<File>& files, const Flight& flight);

/// "18.2 MB", "1.4 GB" (1024-based, as the top bar's storage); empty when unknown.
QString sizeText(qint64 bytes);
/// "18.2 of 59.6 GB": used of total.
QString storageText(double usedMiB, double totalMiB);
/// "11:02:31" in local time; empty when unknown.
QString captureTimeText(qint64 utc);
/// "Oct 19", or "No capture time".
QString flightDateText(const Flight& flight);
/// "11:02 - 11:33" in local time; empty without a time.
QString flightTimeText(const Flight& flight);
/// "22 photos · 2 videos", "1 photo".
QString countsText(int photos, int videos);
/// One folder per flight, from its local start time: "2026-10-19 11-02", or "No capture time".
QString flightFolderName(const Flight& flight);
/// root/<flight folder>/<name as on the payload>.
QString downloadPath(const QString& root, const Flight& flight, const QString& name);
/// Where a capture falls on its flight's timeline strip, 0..1; the middle for a flight of one
/// moment; -1 without a time.
double timelineFraction(qint64 time, const Flight& flight);
/// The index in times nearest to fraction of the flight's span (times in the sheet's order);
/// -1 if empty.
int nearestIndex(const QList<qint64>& times, const Flight& flight, double fraction);

/// Click selection of a desktop file view: a click selects one, Ctrl toggles, Shift selects
/// from the last clicked one (Ctrl + Shift adds that range).
class Selection {
  public:
    void click(const QStringList& names, int index, bool toggle, bool range);
    void selectAll(const QStringList& names);
    void clear();
    /// Drops the names that are gone.
    void retain(const QStringList& names);
    bool contains(const QString& name) const { return selected_.contains(name); }
    int count() const { return static_cast<int>(selected_.size()); }
    bool isEmpty() const { return selected_.isEmpty(); }
    /// The selected names in the view's order.
    QStringList ordered(const QStringList& names) const;

  private:
    QSet<QString> selected_;
    QString anchor_;
};

/// The downloads asked for together, for the transfer row; files queued while one runs join it.
struct Batch {
    int total = 0;
    int done = 0;
    int failed = 0;
    int cancelled = 0;
    qint64 totalBytes = 0;    ///< of the files whose size is known
    qint64 finishedBytes = 0; ///< of those that ended (done, failed or cancelled)
    QString error;            ///< the last failure

    bool running() const { return done + failed + cancelled < total; }
    bool isEmpty() const { return total == 0; }
    /// 0..1 with the file in progress at currentBytes; by bytes when sizes are known, else by count.
    double fraction(qint64 currentBytes) const;
    /// "Downloading 3 of 12"
    QString runningTitle() const;
    /// "12 downloaded", "Nothing downloaded"
    QString finishedTitle() const;
    /// "2 failed · 3 cancelled", empty when all went well
    QString finishedDetail() const;
};

/// "1.2 MB/s"
QString rateText(double bytesPerSecond);

} // namespace Media
