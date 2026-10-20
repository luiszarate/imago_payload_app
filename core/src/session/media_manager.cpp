#include "session/media_manager.h"

#include "platform/clock.h"
#include "platform/thread.h"
#include "util/text.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace imago {

namespace {

// The list and deletes answer in well under a second through Herelink (the web settings page
// does, 2026-10-16); a list of a full card is a bigger page, so it gets longer.
constexpr int kConnectTimeoutMs = 1500;
constexpr int kListTimeoutMs = 15000;
constexpr int kShortTimeoutMs = 8000;
// A download is cut only when nothing arrives for this long (a video is hundreds of MB).
constexpr int kDownloadIdleMs = 30000;
// The EXIF segment is at most 64 KiB, and a few segments may come before it.
constexpr size_t kExifReadMax = 256 * 1024;
constexpr int64_t kRateWindowNs = 3 * kNsPerSec;

// Paths cross the C API as UTF-8; on Windows a narrow path would be read as the ANSI code page.
std::filesystem::path fromUtf8(const std::string& text) {
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const char c : text) {
        utf8.push_back(static_cast<char8_t>(c));
    }
    return std::filesystem::path(utf8);
}

bool isHtml(const web::HttpHead& head) {
    return head.contentType.find("text/html") != std::string::npos;
}

std::string failure(const web::HttpHead& head, const std::string& error) {
    if (head.status == 0) {
        return error.empty() ? "no answer" : error;
    }
    if (!error.empty()) {
        return error;
    }
    return "the payload answered " + std::to_string(head.status);
}

std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

} // namespace

MediaManager::MediaManager(Emit emit) : emit_(std::move(emit)) {}

MediaManager::~MediaManager() { stop(); }

void MediaManager::start(const std::string& host, uint16_t port) {
    stop();
    host_ = host;
    port_ = port;
    stopping_.store(false);
    abortDownload_.store(false);
    control_ = std::thread([this] { controlLoop(); });
    downloader_ = std::thread([this] { downloadLoop(); });
}

void MediaManager::stop() {
    {
        std::lock_guard lock(mutex_);
        jobs_.clear();
        downloads_.clear();
        wanted_.clear();
        deletesPending_ = 0;
        stopping_.store(true);
        abortDownload_.store(true);
    }
    controlWake_.notify_all();
    downloadWake_.notify_all();
    if (control_.joinable()) {
        control_.join(); // a request in flight gives up within 100 ms, or its connect within 1.5 s
    }
    if (downloader_.joinable()) {
        downloader_.join();
    }
    std::lock_guard lock(mutex_);
    downloading_ = false;
}

void MediaManager::refresh(uint32_t id) {
    {
        std::lock_guard lock(mutex_);
        if (status_ != IMAGO_MEDIA_STATUS_READY) {
            status_ = IMAGO_MEDIA_STATUS_LOADING;
            ++revision_;
        }
        jobs_.push_back({Job::Kind::List, id, {}, {}});
    }
    controlWake_.notify_one();
}

void MediaManager::setActive(bool active) {
    {
        std::lock_guard lock(mutex_);
        active_ = active;
    }
    controlWake_.notify_one();
}

void MediaManager::download(uint32_t id, const std::string& name, const std::string& path) {
    {
        std::lock_guard lock(mutex_);
        if (!downloading_ && downloads_.empty()) {
            downloadsDone_ = 0; // a new batch
            downloadsFailed_ = 0;
        }
        downloads_.push_back({id, name, path});
    }
    downloadWake_.notify_one();
}

void MediaManager::cancelDownloads() {
    std::deque<Download> dropped;
    {
        std::lock_guard lock(mutex_);
        dropped.swap(downloads_);
        if (downloading_) {
            abortDownload_.store(true); // the one in progress reports itself
        }
    }
    for (const Download& d : dropped) {
        emit_(d.id, IMAGO_MEDIA_OP_DOWNLOAD, IMAGO_RESULT_CANCELLED, d.name.c_str());
    }
}

void MediaManager::remove(uint32_t id, const std::string& name) {
    {
        std::lock_guard lock(mutex_);
        ++deletesPending_;
        jobs_.push_back({Job::Kind::Delete, id, name, {}});
    }
    controlWake_.notify_one();
}

bool MediaManager::setStorage(uint32_t id, uint32_t index) {
    {
        std::lock_guard lock(mutex_);
        if (index >= storages_.size()) {
            return false;
        }
        jobs_.push_back({Job::Kind::Storage, id, {}, storages_[index].value});
    }
    controlWake_.notify_one();
    return true;
}

void MediaManager::requestThumbnail(const std::string& name) {
    {
        std::lock_guard lock(mutex_);
        wanted_.erase(std::remove(wanted_.begin(), wanted_.end(), name), wanted_.end());
        wanted_.push_back(name);
        if (wanted_.size() > 256) {
            wanted_.erase(wanted_.begin()); // scrolled far past them
        }
    }
    controlWake_.notify_one();
}

imago_media_state MediaManager::state() {
    imago_media_state s{};
    std::lock_guard lock(mutex_);
    s.status = status_;
    s.revision = revision_;
    s.file_count = static_cast<uint32_t>(files_.size());
    s.flight_count = static_cast<uint32_t>(flights_.size());
    text::copyTo(s.error, error_);
    s.storage_count = static_cast<uint32_t>(std::min<size_t>(storages_.size(), IMAGO_MEDIA_MAX_STORAGES));
    for (uint32_t i = 0; i < s.storage_count; ++i) {
        text::copyTo(s.storage_label[i], storages_[i].label.empty() ? storages_[i].value : storages_[i].label);
    }
    s.storage_current = currentStorage_;
    s.downloading = downloading_;
    text::copyTo(s.download_name, downloadName_);
    s.download_bytes = downloadBytes_;
    s.download_total = downloadTotal_;
    s.download_bytes_per_s = downloadRate_;
    s.downloads_queued = static_cast<uint32_t>(downloads_.size());
    s.downloads_done = downloadsDone_;
    s.downloads_failed = downloadsFailed_;
    s.deletes_pending = deletesPending_;
    return s;
}

uint32_t MediaManager::files(imago_media_file* out, uint32_t capacity) {
    std::lock_guard lock(mutex_);
    const uint32_t n = std::min<uint32_t>(capacity, static_cast<uint32_t>(files_.size()));
    for (uint32_t i = 0; i < n && out != nullptr; ++i) {
        const File& f = files_[i];
        imago_media_file& o = out[i];
        o = imago_media_file{};
        text::copyTo(o.name, f.name);
        switch (f.kind) {
        case web::MediaKind::Photo:
            o.kind = IMAGO_MEDIA_PHOTO;
            break;
        case web::MediaKind::Video:
            o.kind = IMAGO_MEDIA_VIDEO;
            break;
        case web::MediaKind::Radiometric:
            o.kind = IMAGO_MEDIA_RADIOMETRIC;
            break;
        case web::MediaKind::Other:
            o.kind = IMAGO_MEDIA_OTHER;
            break;
        }
        o.thermal = f.thermal;
        o.camera = f.shot.camera == web::MediaCamera::Eo   ? IMAGO_MEDIA_CAMERA_EO
                   : f.shot.camera == web::MediaCamera::Ir ? IMAGO_MEDIA_CAMERA_IR
                                                           : IMAGO_MEDIA_CAMERA_UNKNOWN;
        if (f.pair >= 0) {
            text::copyTo(o.pair, files_[static_cast<size_t>(f.pair)].name);
        }
        o.primary = f.pair < 0 || f.shot.camera != web::MediaCamera::Ir;
        o.size_bytes = f.size;
        o.capture_time = f.time;
        o.flight = f.flight;
        const File* source = thumbnailSourceLocked(f);
        o.thumbnail = source != nullptr;
        o.thumbnail_from_pair = source != nullptr && source != &f;
        // Failed for good only when the pair (if any) has nothing to lend either.
        const File* pair = f.pair >= 0 ? &files_[static_cast<size_t>(f.pair)] : nullptr;
        o.thumbnail_failed = source == nullptr && f.thumb == Thumb::Failed &&
                             (pair == nullptr || pair->thumb == Thumb::Failed ||
                              pair->kind != web::MediaKind::Photo);
    }
    return static_cast<uint32_t>(files_.size());
}

uint32_t MediaManager::flights(imago_media_flight* out, uint32_t capacity) {
    std::lock_guard lock(mutex_);
    const uint32_t n = std::min<uint32_t>(capacity, static_cast<uint32_t>(flights_.size()));
    if (out != nullptr) {
        std::copy_n(flights_.begin(), n, out);
    }
    return static_cast<uint32_t>(flights_.size());
}

size_t MediaManager::copyThumbnail(const std::string& name, uint8_t* out, size_t capacity) {
    std::lock_guard lock(mutex_);
    for (File& f : files_) {
        if (f.name != name) {
            continue;
        }
        File* source = const_cast<File*>(thumbnailSourceLocked(f));
        if (source == nullptr) {
            return 0;
        }
        source->lastUse = ++useClock_;
        if (out != nullptr && capacity >= source->thumbnail.size()) {
            std::copy(source->thumbnail.begin(), source->thumbnail.end(), out);
        }
        return source->thumbnail.size();
    }
    return 0;
}

const MediaManager::File* MediaManager::thumbnailSourceLocked(const File& f) const {
    if (f.thumb == Thumb::Ready) {
        return &f;
    }
    if (f.pair >= 0 && f.kind == web::MediaKind::Photo) {
        const File& pair = files_[static_cast<size_t>(f.pair)];
        if (pair.thumb == Thumb::Ready) {
            return &pair;
        }
    }
    return nullptr;
}

std::optional<net::Endpoint> MediaManager::server() {
    std::lock_guard lock(serverMutex_);
    if (!server_) {
        server_ = net::Endpoint::resolve(host_, port_); // blocking: only on these threads
    }
    return server_;
}

void MediaManager::controlLoop() {
    configureCurrentThread("imago-media", ThreadRole::Background);
    while (true) {
        std::optional<Job> job;
        std::optional<File> file;
        {
            std::unique_lock lock(mutex_);
            while (!stopping_.load()) {
                if (!jobs_.empty()) {
                    job = jobs_.front();
                    jobs_.pop_front();
                    break;
                }
                if ((file = nextProbeLocked())) {
                    break;
                }
                controlWake_.wait(lock);
            }
            if (stopping_.load()) {
                return;
            }
        }
        const auto endpoint = server();
        if (!endpoint) {
            if (job) {
                if (job->kind == Job::Kind::Delete) {
                    std::lock_guard lock(mutex_);
                    --deletesPending_;
                } else if (job->kind == Job::Kind::List) {
                    std::lock_guard lock(mutex_);
                    status_ = IMAGO_MEDIA_STATUS_FAILED;
                    error_ = "unknown web host " + host_;
                    ++revision_;
                }
                const imago_media_op op = job->kind == Job::Kind::Delete    ? IMAGO_MEDIA_OP_DELETE
                                          : job->kind == Job::Kind::Storage ? IMAGO_MEDIA_OP_STORAGE
                                                                            : IMAGO_MEDIA_OP_LIST;
                emit_(job->id, op, IMAGO_RESULT_FAILED,
                      job->kind == Job::Kind::Delete ? job->name.c_str() : ("unknown web host " + host_).c_str());
            } else {
                std::lock_guard lock(mutex_);
                for (File& f : files_) {
                    if (f.name == file->name) {
                        f.probed = true; // try again on the next refresh
                    }
                }
            }
            continue;
        }
        if (job) {
            runJob(*job, *endpoint);
        } else {
            const Probe result = probe(*file, *endpoint);
            if (!stopping_.load()) {
                applyProbe(file->name, result);
            }
        }
    }
}

std::optional<MediaManager::File> MediaManager::nextProbeLocked() {
    auto needsProbe = [](const File& f) {
        return !f.probed || (f.kind == web::MediaKind::Photo && f.thumb == Thumb::None);
    };
    while (!wanted_.empty()) {
        const std::string name = wanted_.back();
        wanted_.pop_back();
        for (const File& f : files_) {
            if (f.name != name) {
                continue;
            }
            // A cell on screen shows the pair's picture when its own cannot be had, so the
            // pair (the IR twin of an EO photo) is looked up first: its picture comes whole.
            if (f.pair >= 0) {
                const File& pair = files_[static_cast<size_t>(f.pair)];
                if (f.thumb != Thumb::Ready && needsProbe(pair)) {
                    if (needsProbe(f)) {
                        wanted_.push_back(name); // then the file itself
                    }
                    return pair;
                }
            }
            if (needsProbe(f)) {
                return f;
            }
        }
    }
    if (!active_) {
        return std::nullopt;
    }
    for (const File& f : files_) {
        if (!f.probed) {
            return f;
        }
    }
    return std::nullopt;
}

void MediaManager::runJob(const Job& job, const net::Endpoint& server) {
    switch (job.kind) {
    case Job::Kind::List:
        runList(job.id, server);
        break;
    case Job::Kind::Delete:
        runDelete(job, server);
        break;
    case Job::Kind::Storage:
        runStorage(job, server);
        break;
    }
}

void MediaManager::runList(uint32_t id, const net::Endpoint& server) {
    web::HttpStreamRequest request;
    request.path = web::kMediaListPath;
    request.connectTimeoutMs = kConnectTimeoutMs;
    request.idleTimeoutMs = kShortTimeoutMs;
    request.totalTimeoutMs = kListTimeoutMs;
    std::string page;
    std::string error;
    const web::HttpHead head = web::httpStream(
        server, request, &stopping_, {},
        [&](const char* data, size_t size) {
            page.append(data, size);
            return page.size() < 32u * 1024 * 1024;
        },
        &error);
    if (stopping_.load()) {
        return;
    }
    imago_media_status status = IMAGO_MEDIA_STATUS_READY;
    int32_t result = IMAGO_RESULT_ACCEPTED;
    web::MediaListing listing;
    if (head.status == 200 && error.empty()) {
        listing = web::parseMediaListing(page);
        if (listing.loginRequired) {
            status = IMAGO_MEDIA_STATUS_LOGIN_REQUIRED;
            error = "the payload asks for a login, which this app does not support yet";
            result = IMAGO_RESULT_FAILED;
        }
    } else if (head.status == 404) {
        status = IMAGO_MEDIA_STATUS_STARTING;
        error = "the payload is still starting its cameras";
        result = IMAGO_RESULT_FAILED;
    } else {
        status = IMAGO_MEDIA_STATUS_FAILED;
        error = failure(head, error);
        result = head.status == 0 ? IMAGO_RESULT_TIMEOUT : IMAGO_RESULT_FAILED;
    }
    {
        std::lock_guard lock(mutex_);
        status_ = status;
        error_ = result == IMAGO_RESULT_ACCEPTED ? std::string{} : error;
        if (result == IMAGO_RESULT_ACCEPTED) {
            // Keep what is known of the files still there, so a refresh costs no new probes.
            std::vector<File> next;
            next.reserve(listing.names.size());
            for (const std::string& name : listing.names) {
                if (name.size() >= IMAGO_MEDIA_NAME_MAX) {
                    continue; // cannot cross the C API whole
                }
                const auto known = std::find_if(files_.begin(), files_.end(),
                                                [&](const File& f) { return f.name == name; });
                if (known != files_.end()) {
                    next.push_back(std::move(*known));
                    continue;
                }
                File f;
                f.name = name;
                f.kind = web::mediaKindOf(name);
                f.thermal = f.kind == web::MediaKind::Radiometric || web::isThermalName(name);
                f.shot = web::shotOf(name);
                f.time = web::timeFromName(name);
                f.timeSource = f.time >= 0 ? TimeSource::Name : TimeSource::None;
                next.push_back(std::move(f));
            }
            files_ = std::move(next);
            storages_ = listing.storages;
            currentStorage_ = listing.currentStorage;
            sortLocked();
        }
        ++revision_;
    }
    emit_(id, IMAGO_MEDIA_OP_LIST, result, result == IMAGO_RESULT_ACCEPTED ? "" : error.c_str());
}

void MediaManager::runDelete(const Job& job, const net::Endpoint& server) {
    web::HttpStreamRequest request;
    request.path = web::kMediaDeletePrefix + web::pathEncode(job.name);
    request.connectTimeoutMs = kConnectTimeoutMs;
    request.idleTimeoutMs = kShortTimeoutMs;
    request.totalTimeoutMs = kShortTimeoutMs;
    std::string body;
    std::string error;
    const web::HttpHead head = web::httpStream(
        server, request, &stopping_, {},
        [&](const char* data, size_t size) {
            body.append(data, std::min<size_t>(size, 65536));
            return body.size() < 65536;
        },
        &error);
    if (stopping_.load()) {
        return;
    }
    // The Orus L redirects to the list whether or not it deleted (also for a file already
    // gone; field report 2026-10-20), so the proof is the file answering 404 afterwards.
    // HEAD on /download only: Werkzeug runs the GET handler for a HEAD, so never on /delete.
    bool ok = head.status == 302 || head.status == 303 ||
              (head.status == 200 && !web::parseMediaListing(body).loginRequired);
    if (ok) {
        web::HttpStreamRequest check;
        check.method = "HEAD";
        check.path = web::kMediaDownloadPrefix + web::pathEncode(job.name);
        check.connectTimeoutMs = kConnectTimeoutMs;
        check.idleTimeoutMs = kShortTimeoutMs;
        check.totalTimeoutMs = kShortTimeoutMs;
        std::string checkError;
        const web::HttpHead gone = web::httpStream(server, check, &stopping_, {}, {}, &checkError);
        ok = gone.status == 404;
        if (!ok) {
            error = gone.status == 0 ? "no answer when checking the delete"
                                     : "the payload still has the file";
        }
    }
    {
        std::lock_guard lock(mutex_);
        --deletesPending_;
        if (ok) {
            files_.erase(std::remove_if(files_.begin(), files_.end(),
                                        [&](const File& f) { return f.name == job.name; }),
                         files_.end());
            sortLocked();
        } else {
            error_ = job.name + ": " + failure(head, error);
        }
        ++revision_;
    }
    emit_(job.id, IMAGO_MEDIA_OP_DELETE,
          ok ? IMAGO_RESULT_ACCEPTED : (head.status == 0 ? IMAGO_RESULT_TIMEOUT : IMAGO_RESULT_FAILED),
          job.name.c_str());
}

void MediaManager::runStorage(const Job& job, const net::Endpoint& server) {
    web::HttpStreamRequest request;
    request.method = "POST";
    request.path = web::kMediaStoragePath;
    request.body = "{\"option\": " + jsonString(job.storage) + "}";
    request.contentType = "application/json";
    request.connectTimeoutMs = kConnectTimeoutMs;
    request.idleTimeoutMs = kShortTimeoutMs;
    request.totalTimeoutMs = kShortTimeoutMs;
    std::string body;
    std::string error;
    const web::HttpHead head = web::httpStream(
        server, request, &stopping_, {},
        [&](const char* data, size_t size) {
            body.append(data, size);
            return body.size() < 65536;
        },
        &error);
    if (stopping_.load()) {
        return;
    }
    // The answer is JSON with "ok": true (Gremsy's SDK).
    const bool ok = head.status == 200 && body.find("\"ok\"") != std::string::npos &&
                    body.find("true") != std::string::npos;
    emit_(job.id, IMAGO_MEDIA_OP_STORAGE,
          ok ? IMAGO_RESULT_ACCEPTED : (head.status == 0 ? IMAGO_RESULT_TIMEOUT : IMAGO_RESULT_FAILED),
          ok ? "" : failure(head, error).c_str());
    if (ok) {
        {
            std::lock_guard lock(mutex_);
            files_.clear(); // another storage: nothing known carries over
            flights_.clear();
            status_ = IMAGO_MEDIA_STATUS_LOADING;
            ++revision_;
        }
        runList(job.id, server);
    }
}

MediaManager::Probe MediaManager::probe(const File& file, const net::Endpoint& server) {
    Probe out;
    web::HttpStreamRequest request;
    request.path = web::kMediaDownloadPrefix + web::pathEncode(file.name);
    request.connectTimeoutMs = kConnectTimeoutMs;
    request.idleTimeoutMs = kShortTimeoutMs;
    request.totalTimeoutMs = 20000;
    std::string error;
    if (file.kind != web::MediaKind::Photo) {
        request.method = "HEAD";
        const web::HttpHead head = web::httpStream(server, request, &stopping_, {}, {}, &error);
        out.size = head.status == 200 ? head.contentLength : -1;
        out.serverTime = web::timeFromHttpDate(head.lastModified);
        return out;
    }
    // Only the start of the photo: its EXIF segment holds the thumbnail and the capture time.
    // Reading stops as soon as the segment is in, whether or not the server honours the Range.
    request.headers = "Range: bytes=0-" + std::to_string(kExifReadMax - 1) + "\r\n";
    std::string bytes;
    web::HttpHead head;
    web::httpStream(
        server, request, &stopping_,
        [&](const web::HttpHead& h) {
            head = h;
            return (h.status == 200 || h.status == 206) && !isHtml(h);
        },
        [&](const char* data, size_t size) {
            bytes.append(data, size);
            const auto exif = web::readJpegExif(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
            return exif.needed > bytes.size() && bytes.size() < kExifReadMax;
        },
        &error);
    out.size = head.totalLength;
    out.serverTime = web::timeFromHttpDate(head.lastModified);
    const auto exif = web::readJpegExif(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    out.exifTime = exif.captureTime;
    if (exif.hasThumbnail && exif.thumbnailOffset + exif.thumbnailLength <= bytes.size()) {
        out.thumbnail = bytes.substr(exif.thumbnailOffset, exif.thumbnailLength);
        return out;
    }
    if ((head.status != 200 && head.status != 206) || out.size < 0 || out.size > kWholePhotoMax) {
        return out;
    }
    if (static_cast<int64_t>(bytes.size()) == out.size) {
        out.thumbnail = std::move(bytes); // already whole
        return out;
    }
    // A small photo without an embedded thumbnail (the IR camera's): the photo itself.
    request.headers.clear();
    std::string whole;
    const web::HttpHead full = web::httpStream(
        server, request, &stopping_, [](const web::HttpHead& h) { return h.status == 200 && !isHtml(h); },
        [&](const char* data, size_t size) {
            whole.append(data, size);
            return static_cast<int64_t>(whole.size()) <= kWholePhotoMax;
        },
        &error);
    if (full.status == 200 && error.empty() && static_cast<int64_t>(whole.size()) == out.size) {
        out.thumbnail = std::move(whole);
    }
    return out;
}

void MediaManager::applyProbe(const std::string& name, const Probe& probe) {
    std::lock_guard lock(mutex_);
    const auto it = std::find_if(files_.begin(), files_.end(), [&](const File& f) { return f.name == name; });
    if (it == files_.end()) {
        return; // deleted meanwhile
    }
    File& f = *it;
    f.probed = true;
    if (probe.size >= 0) {
        f.size = probe.size;
    }
    bool moved = false;
    if (probe.exifTime >= 0 && f.timeSource < TimeSource::Exif) {
        moved = f.time != probe.exifTime;
        f.time = probe.exifTime;
        f.timeSource = TimeSource::Exif;
    } else if (probe.serverTime >= 0 && f.timeSource < TimeSource::Server) {
        moved = f.time != probe.serverTime;
        f.time = probe.serverTime;
        f.timeSource = TimeSource::Server;
    }
    if (f.kind == web::MediaKind::Photo) {
        if (probe.thumbnail) {
            f.thumbnail = *probe.thumbnail;
            f.thumb = Thumb::Ready;
            f.lastUse = ++useClock_;
        } else {
            f.thumb = Thumb::Failed;
        }
    } else {
        f.thumb = Thumb::Failed; // videos and TIFFs get a tile, not a picture
    }
    if (moved) {
        sortLocked();
    } else {
        // Sizes changed: the flight totals with them.
        for (imago_media_flight& flight : flights_) {
            flight.size_bytes = 0;
            for (uint32_t i = flight.first_file; i < flight.first_file + flight.file_count; ++i) {
                flight.size_bytes += std::max<int64_t>(files_[i].size, 0);
            }
        }
    }
    trimThumbnailsLocked();
    ++revision_;
}

void MediaManager::sortLocked() {
    std::stable_sort(files_.begin(), files_.end(), [](const File& a, const File& b) {
        if ((a.time >= 0) != (b.time >= 0)) {
            return a.time >= 0;
        }
        if (a.time != b.time) {
            return a.time > b.time;
        }
        return a.name > b.name;
    });
    // Pairs: the other file of the same shot, by index after the sort. A map, because this
    // runs after every probe that moves a time.
    std::unordered_map<std::string, size_t> unpaired;
    for (size_t i = 0; i < files_.size(); ++i) {
        File& f = files_[i];
        f.pair = -1;
        if (f.shot.key.empty()) {
            continue;
        }
        const auto other = unpaired.find(f.shot.key);
        if (other != unpaired.end() && files_[other->second].shot.camera != f.shot.camera) {
            f.pair = static_cast<int>(other->second);
            files_[other->second].pair = static_cast<int>(i);
            unpaired.erase(other);
        } else {
            unpaired[f.shot.key] = i;
        }
    }
    std::vector<int64_t> times;
    times.reserve(files_.size());
    for (const File& f : files_) {
        times.push_back(f.time);
    }
    const std::vector<uint32_t> groups = web::groupIntoFlights(times, kFlightGapSeconds);
    flights_.clear();
    for (size_t i = 0; i < files_.size(); ++i) {
        File& f = files_[i];
        f.flight = groups[i];
        if (flights_.size() <= f.flight) {
            imago_media_flight flight{};
            flight.start_time = f.time;
            flight.end_time = f.time;
            flight.first_file = static_cast<uint32_t>(i);
            flights_.push_back(flight);
        }
        imago_media_flight& flight = flights_[f.flight];
        ++flight.file_count;
        if (f.time >= 0) {
            flight.start_time = std::min(flight.start_time, f.time);
            flight.end_time = std::max(flight.end_time, f.time);
        }
        const bool primary = f.pair < 0 || f.shot.camera != web::MediaCamera::Ir;
        if (f.kind == web::MediaKind::Video && primary) {
            ++flight.videos;
        } else if ((f.kind == web::MediaKind::Photo && primary) || f.kind == web::MediaKind::Radiometric) {
            ++flight.photos;
        }
        flight.size_bytes += std::max<int64_t>(f.size, 0);
    }
}

void MediaManager::trimThumbnailsLocked() {
    size_t total = 0;
    for (const File& f : files_) {
        total += f.thumbnail.size();
    }
    while (total > kThumbnailBudget) {
        File* oldest = nullptr;
        for (File& f : files_) {
            if (f.thumb == Thumb::Ready && (oldest == nullptr || f.lastUse < oldest->lastUse)) {
                oldest = &f;
            }
        }
        if (oldest == nullptr) {
            return;
        }
        total -= oldest->thumbnail.size();
        oldest->thumbnail.clear();
        oldest->thumbnail.shrink_to_fit();
        oldest->thumb = Thumb::None; // fetched again when asked for
    }
}

void MediaManager::downloadLoop() {
    configureCurrentThread("imago-download", ThreadRole::Background);
    while (true) {
        Download job;
        {
            std::unique_lock lock(mutex_);
            downloadWake_.wait(lock, [&] { return stopping_.load() || !downloads_.empty(); });
            if (stopping_.load()) {
                return;
            }
            job = downloads_.front();
            downloads_.pop_front();
            abortDownload_.store(false); // under the lock: a cancel from now on reaches this one
            downloading_ = true;
            downloadName_ = job.name;
            downloadBytes_ = 0;
            downloadTotal_ = -1;
            downloadRate_ = 0;
            for (const File& f : files_) {
                if (f.name == job.name) {
                    downloadTotal_ = f.size;
                }
            }
        }
        const auto endpoint = server();
        if (!endpoint) {
            {
                std::lock_guard lock(mutex_);
                error_ = "unknown web host " + host_;
                finishDownloadLocked(IMAGO_RESULT_FAILED);
            }
            emit_(job.id, IMAGO_MEDIA_OP_DOWNLOAD, IMAGO_RESULT_FAILED, job.name.c_str());
            continue;
        }
        runDownload(job, *endpoint);
    }
}

void MediaManager::runDownload(const Download& job, const net::Endpoint& server) {
    const std::filesystem::path target = fromUtf8(job.path);
    std::filesystem::path part = target;
    part += ".part";
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    std::string error;
    web::HttpHead head;
    int64_t written = 0;
    if (!out) {
        error = "cannot write " + job.path;
    } else {
        web::HttpStreamRequest request;
        request.path = web::kMediaDownloadPrefix + web::pathEncode(job.name);
        request.connectTimeoutMs = kConnectTimeoutMs;
        request.idleTimeoutMs = kDownloadIdleMs;
        std::deque<std::pair<int64_t, int64_t>> samples; // (time, bytes) over kRateWindowNs
        head = web::httpStream(
            server, request, &abortDownload_,
            [&](const web::HttpHead& h) {
                // HTML instead of the file: the login form, or an error page.
                if (h.status != 200 || isHtml(h)) {
                    return false;
                }
                std::lock_guard lock(mutex_);
                if (h.contentLength >= 0) {
                    downloadTotal_ = h.contentLength;
                }
                return true;
            },
            [&](const char* data, size_t size) {
                out.write(data, static_cast<std::streamsize>(size));
                if (!out) {
                    error = "could not write " + job.path + " (disk full?)";
                    return false;
                }
                written += static_cast<int64_t>(size);
                const int64_t now = monotonicNs();
                if (samples.empty() || now - samples.back().first >= 250 * kNsPerMs) {
                    samples.emplace_back(now, written);
                    while (samples.size() > 1 && now - samples.front().first > kRateWindowNs) {
                        samples.pop_front();
                    }
                }
                std::lock_guard lock(mutex_);
                downloadBytes_ = written;
                if (samples.size() > 1) {
                    const double seconds = static_cast<double>(now - samples.front().first) / kNsPerSec;
                    downloadRate_ = seconds > 0
                                        ? static_cast<float>(static_cast<double>(written - samples.front().second) / seconds)
                                        : 0.0f;
                }
                return true;
            },
            &error);
        out.close();
    }
    const bool aborted = abortDownload_.load();
    bool ok = !aborted && error.empty() && head.status == 200 && !isHtml(head) &&
              (head.contentLength < 0 || written == head.contentLength);
    if (ok) {
        std::filesystem::remove(target, ec); // Windows will not rename over a file
        std::filesystem::rename(part, target, ec);
        if (ec) {
            ok = false;
            error = "cannot rename to " + job.path + ": " + ec.message();
        }
    }
    if (!ok) {
        std::filesystem::remove(part, ec);
    }
    if (stopping_.load()) {
        return;
    }
    int32_t result = IMAGO_RESULT_ACCEPTED;
    if (aborted) {
        result = IMAGO_RESULT_CANCELLED;
    } else if (!ok) {
        result = head.status == 0 ? IMAGO_RESULT_TIMEOUT : IMAGO_RESULT_FAILED;
        if (error.empty() && isHtml(head)) {
            error = "the payload answered a web page instead of the file";
        }
    }
    {
        std::lock_guard lock(mutex_);
        if (!ok && !aborted) {
            error_ = job.name + ": " + failure(head, error);
        }
        if (ok) {
            for (File& f : files_) {
                if (f.name == job.name && f.size < 0) {
                    f.size = written;
                    ++revision_;
                }
            }
        }
        finishDownloadLocked(result);
    }
    emit_(job.id, IMAGO_MEDIA_OP_DOWNLOAD, result, job.name.c_str());
}

void MediaManager::finishDownloadLocked(int32_t result) {
    downloading_ = false;
    downloadRate_ = 0;
    if (result == IMAGO_RESULT_ACCEPTED) {
        ++downloadsDone_;
    } else if (result != IMAGO_RESULT_CANCELLED) {
        ++downloadsFailed_;
    }
}

} // namespace imago
