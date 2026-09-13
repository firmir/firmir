add_executable(core-examle core.cpp)
target_link_libraries(core-examle firmir_core)

add_executable(dbus-examle dbus.cpp)
target_link_libraries(dbus-examle firmir_core firmir_dbus)
