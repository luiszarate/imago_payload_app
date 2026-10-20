# The media test photos, built in (see embed_media.cmake).
file(GLOB media_inputs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/testdata/media/*)
list(SORT media_inputs)
set(media_assets ${CMAKE_CURRENT_BINARY_DIR}/media_assets.cpp)
add_custom_command(
    OUTPUT ${media_assets}
    COMMAND ${CMAKE_COMMAND} "-DINPUTS=${media_inputs}" -DOUTPUT=${media_assets}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_media.cmake
    DEPENDS ${media_inputs} ${CMAKE_CURRENT_SOURCE_DIR}/embed_media.cmake
    VERBATIM)

# Simulator library: shared by the orus-sim executable and the core's integration tests.
add_library(orus_sim_lib STATIC orus_simulator.cpp rtsp_server.cpp web_server.cpp media_library.cpp
            ${media_assets})
target_include_directories(orus_sim_lib PUBLIC . ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(orus_sim_lib PUBLIC imago_core imago_mavlink)
imago_set_warnings(orus_sim_lib)

if(IMAGO_BUILD_TOOLS)
    add_executable(orus-sim main.cpp)
    target_link_libraries(orus-sim PRIVATE orus_sim_lib)
    imago_set_warnings(orus-sim)
endif()
