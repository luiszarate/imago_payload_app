cmake_minimum_required(VERSION 3.22)

project(imago_quest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMAGO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(ANDROID_CPP ${IMAGO_ROOT}/android/app/src/main/cpp)

# The core's CMakeLists expects the warning helper from the repository's top-level CMakeLists.
function(imago_set_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                             -Wno-sign-conversion)
endfunction()

set(IMAGO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${IMAGO_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core)

# Khronos' OpenXR loader, from the AAR's prefab module.
find_package(OpenXR REQUIRED CONFIG)

add_library(imago_quest SHARED
    quest_jni.cpp
    operator.cpp
    gl_renderer.cpp
    xr_app.cpp
    # The Android app's decoder (AMediaCodec, no Java on the frame path) and JNI helpers.
    ${ANDROID_CPP}/video_decoder.cpp
    ${ANDROID_CPP}/jni_util.cpp
)
target_include_directories(imago_quest PRIVATE ${ANDROID_CPP})
target_compile_definitions(imago_quest PRIVATE XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES)
target_link_libraries(imago_quest PRIVATE imago_core OpenXR::openxr_loader android mediandk log EGL GLESv3)
imago_set_warnings(imago_quest)
# OpenXR's idiom `XrFoo foo{XR_TYPE_FOO};` leaves `next` to its zero initializer on purpose.
target_compile_options(imago_quest PRIVATE -Wno-missing-field-initializers)
target_link_options(imago_quest PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
