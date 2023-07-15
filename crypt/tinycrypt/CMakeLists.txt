file(GLOB SOURCES ./src/*.c)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(tinycrypt ${SOURCES})
target_include_directories(tinycrypt PUBLIC ./include)