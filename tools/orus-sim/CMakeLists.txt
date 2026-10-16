# Simulator library: shared by the orus-sim executable and the core's integration tests.
add_library(orus_sim_lib STATIC orus_simulator.cpp rtsp_server.cpp web_server.cpp)
target_include_directories(orus_sim_lib PUBLIC . ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(orus_sim_lib PUBLIC imago_core imago_mavlink)
imago_set_warnings(orus_sim_lib)

if(IMAGO_BUILD_TOOLS)
    add_executable(orus-sim main.cpp)
    target_link_libraries(orus-sim PRIVATE orus_sim_lib)
    imago_set_warnings(orus-sim)
endif()
