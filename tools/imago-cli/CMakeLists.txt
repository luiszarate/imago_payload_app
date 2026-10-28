add_executable(imago-cli main.cpp console.cpp)
target_link_libraries(imago-cli PRIVATE imago_core)
imago_set_warnings(imago-cli)
