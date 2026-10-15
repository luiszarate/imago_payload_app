add_executable(imago_core_tests
    test_main.cpp
    test_util.cpp
    test_rtp.cpp
    test_rtsp.cpp
    test_orus.cpp
    test_command_manager.cpp
    test_session.cpp
    test_video.cpp
)
target_include_directories(imago_core_tests PRIVATE ../src)
target_include_directories(imago_core_tests SYSTEM PRIVATE ../third_party/doctest)
# doctest forward-declares std::basic_ostream, which newer MSVC STLs reject.
target_compile_definitions(imago_core_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(imago_core_tests PRIVATE imago_core imago_mavlink orus_sim_lib)
imago_set_warnings(imago_core_tests)

add_test(NAME imago_core_tests COMMAND imago_core_tests)
set_tests_properties(imago_core_tests PROPERTIES TIMEOUT 120)
