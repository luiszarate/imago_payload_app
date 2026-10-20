#pragma once

#include "imago/imago_core.h"
#include "platform/socket.h"
#include "web/http_client.h"
#include "web/orus_media.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace imago {

// The payload's media files (imago_media_*): the list, thumbnails, deletes and storage on one
// thread, downloads on another, so a long download never holds up the list. Never touches
// the control or video path.
class MediaManager {
  public:
    using Emit = std::function<void(uint32_t requestId, imago_media_op op, int32_t result,
                                    const char* text)>;

    explicit MediaManager(Emit emit);
    ~MediaManager();
    MediaManager(const MediaManager&) = delete;
    MediaManager& operator=(const MediaManager&) = delete;

    void start(const std::string& host, uint16_t port);
    void stop();

    // Request IDs come from the session, so they never collide with its other requests.
    void refresh(uint32_t id);
    void setActive(bool active);
    void download(uint32_t id, const std::string& name, const std::string& path);
    void cancelDownloads();
    void remove(uint32_t id, const std::string& name);
    bool setStorage(uint32_t id, uint32_t index);
    void requestThumbnail(const std::string& name);

    imago_media_state state();
    uint32_t files(imago_media_file* out, uint32_t capacity);
    uint32_t flights(imago_media_flight* out, uint32_t capacity);
    size_t copyThumbnail(const std::string& name, uint8_t* out, size_t capacity);

    // Thumbnails kept in memory; the least recently asked for go first beyond it.
    static constexpr size_t kThumbnailBudget = 48u * 1024 * 1024;
    // A photo without an embedded thumbnail is fetched whole up to this size (the IR camera's
    // small JPEGs), else it gets none.
    static constexpr int64_t kWholePhotoMax = 1536 * 1024;
    static constexpr int64_t kFlightGapSeconds = 30 * 60;

  private:
    enum class TimeSource { None, Server, Exif, Name };
    enum class Thumb { None, Ready, Failed };
    struct File {
        std::string name;
        web::MediaKind kind = web::MediaKind::Other;
        bool thermal = false;
        web::MediaShot shot;
        int pair = -1; // index of the other file of the shot, set by sortLocked
        int64_t size = -1;
        int64_t time = -1;
        TimeSource timeSource = TimeSource::None;
        bool probed = false; // size, time and thumbnail looked up once
        Thumb thumb = Thumb::None;
        std::string thumbnail;
        uint64_t lastUse = 0;
        uint32_t flight = 0;
    };
    struct Job {
        enum class Kind { List, Delete, Storage } kind = Kind::List;
        uint32_t id = 0;
        std::string name;
        std::string storage;
    };
    struct Download {
        uint32_t id = 0;
        std::string name;
        std::string path;
    };
    struct Probe {
        int64_t size = -1;
        int64_t serverTime = -1;
        int64_t exifTime = -1;
        std::optional<std::string> thumbnail; // nullopt: failed or not a photo
    };

    void controlLoop();
    void downloadLoop();
    std::optional<net::Endpoint> server();
    void runJob(const Job& job, const net::Endpoint& server);
    void runList(uint32_t id, const net::Endpoint& server);
    void runDelete(const Job& job, const net::Endpoint& server);
    void runStorage(const Job& job, const net::Endpoint& server);
    void runDownload(const Download& job, const net::Endpoint& server);
    Probe probe(const File& file, const net::Endpoint& server);
    // The next file to probe: one asked for, else the newest not probed yet. Locked.
    std::optional<File> nextProbeLocked();
    void applyProbe(const std::string& name, const Probe& probe);
    void sortLocked();
    // The file whose thumbnail stands for f: f itself, or its pair when f has none.
    const File* thumbnailSourceLocked(const File& f) const;
    void trimThumbnailsLocked();
    void finishDownloadLocked(int32_t result);

    Emit emit_;
    std::string host_;
    uint16_t port_ = 0;
    std::optional<net::Endpoint> server_; // resolved once, guarded by serverMutex_
    std::mutex serverMutex_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> abortDownload_{false};
    std::thread control_;
    std::thread downloader_;

    std::mutex mutex_;
    std::condition_variable controlWake_;
    std::condition_variable downloadWake_;
    // Guarded by mutex_:
    std::deque<Job> jobs_;
    std::vector<std::string> wanted_; // thumbnails asked for, latest last
    bool active_ = true;
    std::vector<File> files_;
    std::vector<imago_media_flight> flights_;
    imago_media_status status_ = IMAGO_MEDIA_STATUS_IDLE;
    std::string error_;
    uint32_t revision_ = 0;
    uint64_t useClock_ = 0;
    std::vector<web::MediaStorage> storages_;
    int currentStorage_ = -1;
    uint32_t deletesPending_ = 0;
    std::deque<Download> downloads_;
    bool downloading_ = false;
    std::string downloadName_;
    int64_t downloadBytes_ = 0;
    int64_t downloadTotal_ = -1;
    float downloadRate_ = 0;
    uint32_t downloadsDone_ = 0;
    uint32_t downloadsFailed_ = 0;
};

} // namespace imago
