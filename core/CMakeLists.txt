# Header-only MAVLink C library, included as SYSTEM so its warnings stay out of our builds.
add_library(imago_mavlink INTERFACE)
target_include_directories(imago_mavlink SYSTEM INTERFACE third_party/mavlink)

add_library(imago_core STATIC
    src/platform/socket.cpp
    src/platform/thread.cpp
    src/util/base64.cpp
    src/util/md5.cpp
    src/util/stats.cpp
    src/util/text.cpp
    src/mavlink/mavlink_link.cpp
    src/mavlink/tlog_filter.cpp
    src/mavlink/tlog_writer.cpp
    src/payload/command_manager.cpp
    src/payload/orus_protocol.cpp
    src/payload/payload_model.cpp
    src/rtp/rtp_packet.cpp
    src/rtp/depacketizer.cpp
    src/rtsp/rtsp_url.cpp
    src/rtsp/sdp.cpp
    src/rtsp/rtsp_message.cpp
    src/rtsp/rtsp_client.cpp
    src/video/lag_tracker.cpp
    src/video/video_receiver.cpp
    src/session/session.cpp
    src/session/capi.cpp
)
target_include_directories(imago_core
    PUBLIC include
    PRIVATE src)
target_compile_features(imago_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(imago_core PUBLIC Threads::Threads PRIVATE imago_mavlink)
if(WIN32)
    # Windows 10 and later: inet_pton, SetThreadDescription. winmm: timeBeginPeriod.
    target_compile_definitions(imago_core
        PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
        PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(imago_core PUBLIC ws2_32 winmm)
endif()
imago_set_warnings(imago_core)

if(IMAGO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
