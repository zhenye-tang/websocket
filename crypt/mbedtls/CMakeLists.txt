file(GLOB SOURCES ./library/*.c ./ports/src/*.c)
add_library(mbedtls ${SOURCES})
target_include_directories(mbedtls PUBLIC ./include ./ports/inc)