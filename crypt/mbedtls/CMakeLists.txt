file(GLOB SOURCES ./library/*.c)
add_library(mbedtls ${SOURCES})
target_include_directories(mbedtls PUBLIC ./include)