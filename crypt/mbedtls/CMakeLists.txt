file(GLOB SOURCES ./library/*.c ./ports/src/*.c)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(mbedtls ${SOURCES})
target_include_directories(mbedtls PUBLIC ./include ./ports/inc)