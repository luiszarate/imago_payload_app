cmake_minimum_required(VERSION 3.22)

project(imago_payload_control VERSION 0.1.0 LANGUAGES C CXX)

option(IMAGO_BUILD_TESTS "Build the core unit tests" ON)
option(IMAGO_BUILD_TOOLS "Build the Orus L simulator and the CLI" ON)
option(IMAGO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(IMAGO_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags shared by every first-party target.
function(imago_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
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
    if(IMAGO_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

if(IMAGO_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

# The simulator library is also used by the core's integration tests.
if(IMAGO_BUILD_TESTS OR IMAGO_BUILD_TOOLS)
    add_subdirectory(tools/orus-sim)
endif()
if(IMAGO_BUILD_TOOLS)
    add_subdirectory(tools/imago-cli)
endif()
