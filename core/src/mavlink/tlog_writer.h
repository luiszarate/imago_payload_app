#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imago {

// Telemetry log (.tlog): each record is 8 bytes of big-endian microseconds since 1970 followed
// by one raw MAVLink packet, as QGroundControl, MAVExplorer and pymavlink read it.
//
// record() only copies into memory under a short lock, so the control path never waits for
// the disk; a writer thread flushes every 250 ms. If the disk falls far behind, records are
// dropped rather than letting memory grow.
class TlogWriter {
  public:
    TlogWriter() = default;
    ~TlogWriter();
    TlogWriter(const TlogWriter&) = delete;
    TlogWriter& operator=(const TlogWriter&) = delete;

    bool open(const std::string& path, std::string* error);
    // Writes what is pending and closes the file.
    void close();

    // Any thread.
    void record(const uint8_t* packet, size_t length);

    // After close(): false if a write failed (full disk) or records were dropped.
    bool complete() const { return !incomplete_.load(); }

  private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<uint8_t> pending_; // guarded by mutex_
    bool closing_ = false;         // guarded by mutex_
    std::FILE* file_ = nullptr;    // writer thread only, once open
    std::atomic<bool> incomplete_{false};
    std::thread thread_;
};

} // namespace imago
