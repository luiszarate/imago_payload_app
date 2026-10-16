#pragma once

#include "imago/imago_core.h"
#include "mavlink/mavlink_link.h"
#include "payload/command_manager.h"
#include "payload/orus_protocol.h"
#include "payload/payload_model.h"
#include "util/stats.h"
#include "video/video_receiver.h"
#include "web/orus_web.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <deque>
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
    void setGimbalProtocol(imago_gimbal_protocol protocol);
    uint32_t takeGimbalControl(bool take);

    // Camera
    uint32_t setCameraMode(imago_camera_mode mode);
    uint32_t takePhoto();
    uint32_t setRecording(bool recording);
    uint32_t zoom(int direction);
    uint32_t zoomTo(float level);
    uint32_t zoomRange(float percent);
    int startRecording(const std::string& path);
    int stopRecording();
    void setRecordingDetail(bool compact);

    // Web settings (imago_web_*)
    uint32_t webReadIrSettings();
    uint32_t webSetIsotherm(bool enabled, int thresholdC, int measurementRange);
    uint32_t webRestart(bool fullReboot);
    imago_web_ir_settings webIrSettings();
    void setZoomTarget(float level);
    uint32_t focus(int direction);
    uint32_t autoFocus();
    uint32_t setCameraParam(std::string_view id, uint32_t value);
    int readCameraParam(std::string_view id);

    // Tracking
    uint32_t setTrackingMode(imago_tracking_mode mode);
    uint32_t trackAt(float x, float y, float w, float h);
    uint32_t pointAt(float x, float y);

    uint32_t irFfc();
    uint32_t setIrFfcMode(bool automatic);
    uint32_t setIsotherm(bool enabled, float lowC, float highC);
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
    void serviceZoomTarget(int64_t nowNs);
    void serviceParamReads(int64_t nowNs);
    void emitCameraParam(const char (&id)[16], const char (&value)[128]);
    float zoomRangeMax() const;
    int32_t zoomMode() const;
    float clampZoom(float level) const;
    float snapZoom(float level) const;
    mavlink_param_ext_set_t zoomCommand(float level) const;
    void noteOwnZoom(int direction, float target, int64_t coastNs);
    void watchZoom(int64_t nowNs);
    void recordStats(int64_t nowNs);
    void expectTracking(bool stop, uint32_t requestId, float x, float y);
    void clearTrackingExpectation();
    void watchTracking(int64_t nowNs);
    void emitResult(const CommandManager::Result& r);
    void emit(imago_event_type type, uint32_t requestId, uint32_t code, int32_t result,
              const char* text);
    void onStreamUri(const std::string& uri, uint32_t streamId);
    void ensureVideoReceiver();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Tracked command: registered with the command manager and sent now. `registered` runs
    // with the request id after registration and before the send, so an answer can never
    // arrive before whatever it sets up.
    uint32_t sendTracked(CommandManager::Message message, int retries = -1,
                         const std::function<void(uint32_t)>& registered = {});

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

    // Web settings: one request at a time, on their own thread.
    struct WebJob {
        enum class Kind { Read, Isotherm, Restart } kind = Kind::Read;
        uint32_t id = 0;
        bool enabled = false;
        int thresholdC = 0;
        int measurementRange = 0;
        bool fullReboot = false;
    };
    uint32_t queueWeb(WebJob job);
    void webLoop();
    void runWebJob(const WebJob& job, const net::Endpoint& server);
    // Reads the IR page into webSettings_; false (and why) if it could not.
    bool readIrPage(const net::Endpoint& server, int& status, std::string& error);
    std::string webHost_;
    uint16_t webPort_ = web::kOrusWebPort;
    std::thread webThread_;
    std::mutex webMutex_;
    std::condition_variable webWake_;
    std::deque<WebJob> webJobs_;          // guarded by webMutex_
    imago_web_ir_settings webSettings_{}; // guarded by webMutex_
    std::atomic<bool> webStopping_{false};

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
    std::atomic<int> gimbalProtocol_{IMAGO_GIMBAL_PROTOCOL_V2};

    // Zoom level streamed from a dragged control: written by any thread, serviced by the
    // network thread.
    std::mutex zoomMutex_;
    float zoomTarget_ = 0;     // 0 = none
    bool zoomDirty_ = false;   // changed since the last send
    bool zoomRepeat_ = false;  // the final level still has to be sent once more
    int64_t zoomLastSentNs_ = 0;

    // Camera settings to read, sent one at a time by the network thread.
    std::mutex paramReadMutex_;
    std::deque<std::string> paramReads_;
    int64_t nextParamReadNs_ = 0;

    // This session's latest zoom intent, written by any caller and read by the network thread
    // to tell its own zoom from someone else's (IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY).
    std::atomic<int> ownZoomDirection_{0};
    std::atomic<float> ownZoomTarget_{0};        // 0 = none
    std::atomic<int64_t> ownZoomGraceUntilNs_{0}; // the lens may still be coasting until then
    std::atomic<uint32_t> ownZoomSerial_{0};      // bumped with every intent
    // Network thread only.
    uint32_t watchedZoomSerial_ = 0;
    bool zoomIntentLost_ = false;  // someone else moved the zoom since our last intent
    float zoomRefLevel_ = 0;       // held level, furthest level of our move, or nearest to target
    float zoomRefDistance_ = 0;    // log distance to our target at its nearest
    int64_t externalZoomQuietUntilNs_ = 0;

    // The last tracking command (stop, or track a point) and what the payload's tracker
    // should show for it, checked by the network thread once it had time to react after
    // accepting it. A later tracking command of any kind replaces it.
    struct TrackingExpectation {
        bool pending = false;
        bool stop = false;
        bool accepted = false; // the payload acknowledged the command, at acceptedNs
        uint32_t requestId = 0;
        int64_t acceptedNs = 0;
        float x = 0, y = 0;             // point asked for (normalized)
        int32_t stateBefore = 0;
        bool boxBefore = false;
        float boxXBefore = 0, boxYBefore = 0;
        bool gimbalBefore = false;
        float pitchBefore = 0, yawBefore = 0; // gimbal attitude, degrees
    };
    std::mutex trackingMutex_;
    TrackingExpectation tracking_; // guarded by trackingMutex_

    std::mutex recordingMutex_;
    std::shared_ptr<TlogWriter> recorder_; // guarded by recordingMutex_
    std::atomic<bool> compactRecording_{false};
    // What a compact recording keeps: the payload's own components.
    std::shared_ptr<TlogFilter> compactFilter_ = std::make_shared<TlogFilter>([](uint8_t c) {
        return c == orus::kPayloadComponentId || orus::isCameraComponent(c) ||
               orus::isGimbalComponent(c);
    });

    // Network thread timers.
    int64_t nextHeartbeatNs_ = 0;
    int64_t nextKeepAwakeNs_ = 0;
    int64_t nextStatsRecordNs_ = 0;
    mavlink_status_t statsStatus_{}; // network thread only
    int64_t nextStreamInfoNs_ = 0;
    int64_t nextStatusNs_ = 0;
    int64_t nextIntervalsNs_ = 0;
    int64_t nextZoomModeNs_ = 0;

    std::mutex metricsMutex_;
    float rttLastMs_ = 0;
    float rttAvgMs_ = 0;
    WindowedPercentile decodeMs_;
    WindowedPercentile presentMs_;
};

} // namespace imago
