#include "core/AiDetection.h"

std::optional<bool> AiDetectionButton::press(qint64 nowMs, uint32_t detectionFrame) {
    if (lastPressMs_ && nowMs - *lastPressMs_ <= kRepressMs) {
        return std::nullopt;
    }
    lastPressMs_ = nowMs;
    enabled_ = !enabled_;
    restartWait(detectionFrame);
    return enabled_;
}

bool AiDetectionButton::update(qint64 nowMs, bool linkUp, uint32_t detectionFrame, int detectionCount) {
    if (!linkUp) {
        // Link lost: the wait starts again when it is back, and the notice may come again (as
        // on iOS), since the payload may have restarted without AI.
        restartWait(detectionFrame);
        return false;
    }
    if (!enabled_ || warned_) {
        return false;
    }
    if (!waitingSinceMs_) {
        // The 6 s run from the first poll with the link up, from the boxes seen then.
        waitingSinceMs_ = nowMs;
        frameAtStart_ = detectionFrame;
        return false;
    }
    if (detectionFrame != frameAtStart_ || detectionCount > 0) {
        // Boxes arrive: nothing to say for this switch-on, until the link drops.
        warned_ = true;
        return false;
    }
    if (nowMs - *waitingSinceMs_ < kNoBoxesMs) {
        return false;
    }
    warned_ = true;
    return true;
}

void AiDetectionButton::reset() {
    enabled_ = false;
    lastPressMs_.reset();
    restartWait(0);
}

void AiDetectionButton::restartWait(uint32_t detectionFrame) {
    waitingSinceMs_.reset();
    frameAtStart_ = detectionFrame;
    warned_ = false;
}
