file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(firmir_core SHARED ${SOURCES})
target_include_directories(firmir_core PUBLIC include)
target_include_directories(firmir_core PRIVATE include_private)
