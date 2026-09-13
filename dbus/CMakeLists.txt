file(GLOB SOURCES "*.cpp")
add_library(firmir_dbus SHARED ${SOURCES})
target_include_directories(firmir_dbus PUBLIC include)
target_include_directories(firmir_dbus PRIVATE include_private ../core/include ../core/include_private)
