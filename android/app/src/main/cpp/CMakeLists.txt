cmake_minimum_required(VERSION 3.22)

project(imago_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMAGO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

# The core's CMakeLists expects the warning helper from the repository's top-level CMakeLists.
function(imago_set_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                             -Wno-sign-conversion)
endfunction()

set(IMAGO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${IMAGO_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core)

add_library(imago_jni SHARED
    jni_core.cpp
    jni_util.cpp
    state_writer.cpp
    video_decoder.cpp
)
target_link_libraries(imago_jni PRIVATE imago_core android mediandk log)
imago_set_warnings(imago_jni)
# 16 KB pages (Android 15+ devices) and a smaller library.
target_link_options(imago_jni PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
