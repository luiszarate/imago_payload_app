add_executable(imago-cli main.cpp console.cpp terminal.cpp output_pane.cpp)
target_link_libraries(imago-cli PRIVATE imago_core)
imago_set_warnings(imago-cli)

if(IMAGO_BUILD_TESTS)
    add_executable(imago_cli_tests test_output_pane.cpp output_pane.cpp)
    target_include_directories(imago_cli_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/core/third_party/doctest)
    # doctest forward-declares std::basic_ostream, which newer MSVC STLs reject.
    target_compile_definitions(imago_cli_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
    imago_set_warnings(imago_cli_tests)
    add_test(NAME imago_cli_tests COMMAND imago_cli_tests)
endif()
