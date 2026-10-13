#pragma once

#include "imago/imago_core.h"
#include "mavlink/mavlink_link.h"
#include "payload/command_manager.h"
#include "payload/orus_protocol.h"
#include "payload/payload_model.h"
#include "util/stats.h"
#include "video/video_receiver.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace imago {

class Session {
  public:
    Session(const imago_config& config, imago_event_fn onEvent, imago_video_frame_fn onFrame,
            void* user);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int start();
    void stop();
    void getState(imago_state& out);

    int startVideo(const char* url);
    void stopVideo();
    void reportPresented(int64_t receiveNs, int64_t decodedNs, int64_t presentedNs);

    // Gimbal
    void setGimbalRate(float pitchDps, float yawDps);
    uint32_t setGimbalAngle(float pitchDeg, float yawDeg);
    uint32_t setGimbalMode(imago_gimbal_mode mode);

    // Camera
    uint32_t setCameraMode(imago_camera_mode mode);
    uint32_t takePhoto();
    uint32_t setRecording(bool recording);
    uint32_t zoom(int direction);
    uint32_t zoomTo(float level);
    uint32_t focus(int direction);
    uint32_t autoFocus();
    uint32_t setCameraParam(std::string_view id, uint32_t value);

    // Tracking
    uint32_t setTrackingMode(imago_tracking_mode mode);
    uint32_t trackAt(float x, float y, float w, float h);
    uint32_t pointAt(float x, float y);

    uint32_t streamSetting(uint32_t cameraId, uint32_t setting, uint32_t value);

  private:
    void networkLoop();
    void handleMessage(const mavlink_message_t& msg, int64_t nowNs);
    void onLinkUp(int64_t nowNs);
    void onLinkDown();
    void sendDataIntervals();
    void periodicRequests(int64_t nowNs);
    void sendGimbalRate(float pitchDps, float yawDps, int64_t nowNs);
    void serviceGimbalRate(int64_t nowNs);
    void emitResult(const CommandManager::Result& r);
    void emit(imago_event_type type, uint32_t requestId, uint32_t code, int32_t result,
              const char* text);
    void onStreamUri(const std::string& uri, uint32_t streamId);
    void ensureVideoReceiver();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Tracked command: registered with the command manager and sent now.
    uint32_t sendTracked(CommandManager::Message message, int retries = -1);

    // Config with owned strings.
    imago_config config_{};
    std::string payloadIp_;
    std::string rtspUrl_;
    net::Endpoint remote_;
    uint16_t localPort_ = 0;

    imago_event_fn onEvent_;
    imago_video_frame_fn onFrame_;
    void* user_;

    MavlinkLink link_;
    PayloadModel model_;
    CommandManager commands_;
    std::unique_ptr<VideoReceiver> video_;
    std::mutex videoMutex_;
    std::string announcedUrl_; // guarded by videoMutex_

    std::thread networkThread_;
    std::atomic<bool> running_{false};
    std::mutex lifecycleMutex_;

    // Gimbal rate command: written by any thread, serviced by the network thread.
    std::mutex gimbalMutex_;
    float gimbalPitchDps_ = 0;
    float gimbalYawDps_ = 0;
    int gimbalStopRepeats_ = 0;
    int64_t gimbalLastSentNs_ = 0;
    int64_t gimbalPeriodNs_ = 0;

    // Network thread timers.
    int64_t nextHeartbeatNs_ = 0;
    int64_t nextStreamInfoNs_ = 0;
    int64_t nextStatusNs_ = 0;
    int64_t nextIntervalsNs_ = 0;

    std::mutex metricsMutex_;
    float rttLastMs_ = 0;
    float rttAvgMs_ = 0;
    WindowedPercentile decodeMs_;
    WindowedPercentile presentMs_;
};

} // namespace imago
