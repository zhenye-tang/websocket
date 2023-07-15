file(GLOB SOURCES  ./websocket/*.c)

# Set static lib output path
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Generate static lib
add_library(websocket-core ${SOURCES})
# Add header file search path for websocket-core
target_include_directories(websocket-core PUBLIC ./include)
# websocket-core depends on mbedtls
target_link_libraries(websocket-core mbedtls)