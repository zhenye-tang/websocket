cmake_minimum_required(VERSION 3.21)

file(GLOB SOURCES ./src/*.c ./port/*.c)
add_library(websocket ${SOURCES})
target_include_directories(websocket PUBLIC ./inc)

if(WIN32)
    set(WIN32_SOCKET ws2_32)
endif ()

if(UNIX)
    set(USING_PTHREAD pthread)
endif ()

target_link_libraries(websocket PRIVATE mbedtls tinycrypt ${USING_PTHREAD} ${WIN32_SOCKET})

# installation configuration
install(TARGETS websocket DESTINATION lib/${ARCH})
install(DIRECTORY inc/ DESTINATION include)
