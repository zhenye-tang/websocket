file(GLOB SOURCES ./src/*.c)
add_library(tinycrypt ${SOURCES})
target_include_directories(tinycrypt PUBLIC ./include)