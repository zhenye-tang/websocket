cmake_minimum_required(VERSION 3.21)

project(hello-server)

file(GLOB SOURCES *.c)

# Generate executable
add_executable(hello-server
        ${SOURCES}
)

target_link_libraries(hello-server websocket)