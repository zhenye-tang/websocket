
cmake_minimum_required(VERSION 3.21)

project(websocket)

# Set cmake input parameters
option(WEBSOCKET_USING_MBED_TLS "Use the mbedtls library to support wss"  OFF)
if (WEBSOCKET_USING_MBED_TLS)
    add_definitions(-DWEBSOCKET_USING_MBED_TLS)
    message("Use the mbedtls library to support wss")
else()
    message("do not use mbedtls")
endif()

# Set compilation parameters
add_compile_options(-Wall -g -O0)
# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Customize definition
add_definitions(-DWEBSOCKET_USING_MBED_TLS)
# Add subdirectory to search path
add_subdirectory(${PROJECT_SOURCE_DIR}/crypt)
add_subdirectory(${PROJECT_SOURCE_DIR}/src)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Generate executable
add_executable(${CMAKE_PROJECT_NAME} 
            ./sample/main.c
            ./port/websocket_port.c
            )

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} Threads::Threads)
target_link_libraries(${CMAKE_PROJECT_NAME} tinycrypt)
target_link_libraries(${CMAKE_PROJECT_NAME} websocket-core)