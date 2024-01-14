file(GLOB SOURCES ./src/*.c)

add_library(tinycrypt ${SOURCES})
target_include_directories(tinycrypt PUBLIC ./include)

# installation configuration
install(TARGETS tinycrypt DESTINATION lib/${ARCH})
install(DIRECTORY include/ DESTINATION include)
