cmake_minimum_required(VERSION 3.22)

project(imago_desktop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IMAGO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(IMAGO_DESKTOP_TESTS "Build the desktop logic tests" ON)

set(IMAGO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The core's CMakeLists expects the warning helper from the repository's top-level CMakeLists.
function(imago_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(IMAGO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                                 -Wno-sign-conversion)
        if(IMAGO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

set(IMAGO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${IMAGO_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core)

# Qt under the LGPLv3: linked dynamically, no GPL-only modules.
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick QuickControls2 Multimedia)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.8 COMPONENTS DBus)
endif()
qt_standard_project_setup(REQUIRES 6.8)

# FFmpeg (LGPL build, shared libraries): only libavcodec and libavutil, for decoding.
# Qt ships its own FFmpeg libraries without headers; they must not be picked up here.
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg with include/ and lib/ (Windows); empty: pkg-config")
add_library(imago_ffmpeg INTERFACE)
if(FFMPEG_ROOT)
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h PATHS ${FFMPEG_ROOT}/include NO_DEFAULT_PATH REQUIRED)
    find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
    find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
    target_include_directories(imago_ffmpeg SYSTEM INTERFACE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(imago_ffmpeg INTERFACE ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)
    target_link_libraries(imago_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

# SDL2 (zlib license): gamepads, HOTAS and other joysticks.
find_package(SDL2 CONFIG REQUIRED)
if(TARGET SDL2::SDL2)
    set(IMAGO_SDL2 SDL2::SDL2)
else()
    set(IMAGO_SDL2 SDL2::SDL2-static)
endif()

# Logic shared by the app and its tests: no QML, no video.
add_library(imago_desktop_logic STATIC
    src/core/ConnectionProfile.cpp
    src/core/ConnectionProfile.h
    src/control/GimbalInputShaper.cpp
    src/control/GimbalInputShaper.h
    src/control/GamepadMapping.cpp
    src/control/GamepadMapping.h
)
target_include_directories(imago_desktop_logic PUBLIC src)
target_link_libraries(imago_desktop_logic PUBLIC Qt6::Core imago_core)
imago_set_warnings(imago_desktop_logic)

qt_add_executable(imago_payload_control WIN32
    src/main.cpp
    src/app/SystemIntegration.cpp
    src/app/SystemIntegration.h
    src/control/GamepadController.cpp
    src/control/GamepadController.h
    src/core/CameraSettings.cpp
    src/core/CameraSettings.h
    src/core/PayloadSession.cpp
    src/core/PayloadSession.h
    src/video/VideoDecoder.cpp
    src/video/VideoDecoder.h
)
set_target_properties(imago_payload_control PROPERTIES OUTPUT_NAME "ImagoPayloadControl")

set_source_files_properties(qml/Theme.qml qml/AppFont.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(imago_payload_control
    URI Imago
    VERSION 1.0
    SOURCES
        src/app/AppModel.cpp
        src/app/AppModel.h
        src/core/PayloadStatus.cpp
        src/core/PayloadStatus.h
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/AppFont.qml
        qml/Icon.qml
        qml/Components/Panel.qml
        qml/Components/RailButton.qml
        qml/Components/HoldButton.qml
        qml/Components/StatusChip.qml
        qml/Components/IconPill.qml
        qml/Components/Wordmark.qml
        qml/Components/CheckMenu.qml
        qml/Components/ToolTipText.qml
        qml/Operator/TopBar.qml
        qml/Operator/CameraRail.qml
        qml/Operator/ZoomControl.qml
        qml/Operator/GimbalRail.qml
        qml/Operator/Joystick.qml
        qml/Operator/VideoOverlay.qml
        qml/Operator/HUDView.qml
        qml/Operator/ConnectPanel.qml
        qml/Operator/AttitudeIndicator.qml
        qml/Operator/ToastView.qml
        qml/Operator/VideoLagBadge.qml
        qml/Forms/FormPage.qml
        qml/Forms/FormSection.qml
        qml/Forms/FormRow.qml
        qml/Forms/RowDivider.qml
        qml/Forms/SwitchRow.qml
        qml/Forms/ChoiceRow.qml
        qml/Forms/SliderRow.qml
        qml/Forms/BarButton.qml
        qml/Forms/BusyIndicatorSmall.qml
        qml/Forms/TextFieldRow.qml
        qml/Forms/PortFieldRow.qml
        qml/Pages/SettingsPage.qml
        qml/Pages/GamepadSettings.qml
        qml/Pages/ConnectionsPage.qml
        qml/Pages/ProfileEditor.qml
    RESOURCES
        resources/fonts/MaterialIcons-Regular.ttf
)

# The brand fonts as static instances, shared with the Android app.
qt_add_resources(imago_payload_control "fonts"
    PREFIX "/fonts"
    BASE ${IMAGO_ROOT}/android/app/src/main/res/font
    FILES
        ${IMAGO_ROOT}/android/app/src/main/res/font/manrope_400.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/manrope_500.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/manrope_600.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/manrope_700.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/space_grotesk_500.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/space_grotesk_600.ttf
        ${IMAGO_ROOT}/android/app/src/main/res/font/space_grotesk_700.ttf
)
qt_add_resources(imago_payload_control "images"
    PREFIX "/images"
    BASE resources
    FILES resources/icon/imago-payload-control.png
)

# The generated QML type registrations include the registered headers by file name.
target_include_directories(imago_payload_control PRIVATE src src/app src/core)
target_compile_definitions(imago_payload_control PRIVATE
    IMAGO_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(imago_payload_control PRIVATE
    imago_desktop_logic imago_core imago_ffmpeg ${IMAGO_SDL2}
    Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
if(TARGET Qt6::DBus)
    target_link_libraries(imago_payload_control PRIVATE Qt6::DBus)
    target_compile_definitions(imago_payload_control PRIVATE IMAGO_HAVE_DBUS)
endif()
if(WIN32)
    target_sources(imago_payload_control PRIVATE resources/windows/app.rc)
    target_link_libraries(imago_payload_control PRIVATE winmm)
endif()
imago_set_warnings(imago_payload_control)

include(GNUInstallDirs)
install(TARGETS imago_payload_control
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES resources/linux/aero.imago.PayloadControl.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icon/imago-payload-control.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME aero.imago.PayloadControl.png)
endif()

if(IMAGO_DESKTOP_TESTS)
    enable_testing()
    add_executable(imago_desktop_tests tests/test_main.cpp tests/test_logic.cpp)
    target_include_directories(imago_desktop_tests SYSTEM PRIVATE ${IMAGO_ROOT}/core/third_party/doctest)
    target_link_libraries(imago_desktop_tests PRIVATE imago_desktop_logic)
    imago_set_warnings(imago_desktop_tests)
    add_test(NAME imago_desktop_tests COMMAND imago_desktop_tests)
endif()
