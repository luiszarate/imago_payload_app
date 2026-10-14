#include "mavlink/tlog_writer.h"

#include "platform/thread.h"

#include <cerrno>
#include <chrono>
#include <cstring>

namespace imago {

namespace {

// Pending bytes beyond this are dropped: about a minute of a busy link.
constexpr size_t kMaxPendingBytes = 8 * 1024 * 1024;
constexpr auto kFlushPeriod = std::chrono::milliseconds(250);

uint64_t unixMicroseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

TlogWriter::~TlogWriter() { close(); }

bool TlogWriter::open(const std::string& path, std::string* error) {
    close();
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        if (error != nullptr) *error = path + ": " + std::strerror(errno);
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        file_ = file;
        closing_ = false;
        pending_.clear();
        incomplete_ = false;
    }
    thread_ = std::thread([this] { run(); });
    return true;
}

void TlogWriter::close() {
    {
        std::lock_guard lock(mutex_);
        if (file_ == nullptr) {
            return;
        }
        closing_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard lock(mutex_);
    std::fclose(file_);
    file_ = nullptr;
}

void TlogWriter::record(const uint8_t* packet, size_t length) {
    const uint64_t us = unixMicroseconds();
    std::lock_guard lock(mutex_);
    if (file_ == nullptr || closing_) {
        return;
    }
    if (pending_.size() + 8 + length > kMaxPendingBytes) {
        incomplete_ = true;
        return;
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        pending_.push_back(static_cast<uint8_t>(us >> shift));
    }
    pending_.insert(pending_.end(), packet, packet + length);
}

void TlogWriter::run() {
    configureCurrentThread("imago-tlog", ThreadRole::Background);
    std::vector<uint8_t> batch;
    bool done = false;
    while (!done) {
        std::FILE* file = nullptr;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, kFlushPeriod, [this] { return closing_; });
            done = closing_;
            batch.swap(pending_);
            file = file_;
        }
        if (!batch.empty()) {
            if (std::fwrite(batch.data(), 1, batch.size(), file) != batch.size() ||
                std::fflush(file) != 0) {
                incomplete_ = true;
            }
            batch.clear();
        }
    }
}

} // namespace imago
