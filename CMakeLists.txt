
cmake_minimum_required(VERSION 3.21)

project(websocket)

option(WEBSOCKET_USING_MBED_TLS "Use the mbedtls library to support wss"  OFF)
if (WEBSOCKET_USING_MBED_TLS)
    add_definitions(-DWEBSOCKET_USING_MBED_TLS)
    message("Use the mbedtls library to support wss")
else()
    message("do not use mbedtls")
endif()

add_compile_options(-Wall -g -O0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_definitions(-DWEBSOCKET_USING_MBED_TLS)

add_subdirectory(${PROJECT_SOURCE_DIR}/crypt)
include_directories(${PROJECT_SOURCE_DIR}/inc)

add_executable(${CMAKE_PROJECT_NAME} 
            ./sample/main.c
            ./src/websocket.c
            ./src/websocket_service.c
            ./port/websocket_port.c
            )


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} Threads::Threads)
target_link_libraries(${CMAKE_PROJECT_NAME} tinycrypt)
target_link_libraries(${CMAKE_PROJECT_NAME} mbedtls)