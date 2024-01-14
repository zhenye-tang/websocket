file(GLOB SOURCES ./library/*.c ./ports/src/*.c)

add_library(mbedtls ${SOURCES})
target_include_directories(mbedtls PUBLIC ./include ./ports/inc)

# installation configuration
install(TARGETS mbedtls DESTINATION lib/${ARCH})
install(DIRECTORY include/ DESTINATION include)
