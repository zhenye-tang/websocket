cmake_minimum_required(VERSION 3.21)

include(cmake/cross-compile.cmake)

project(websocket)

set(CMAKE_INSTALL_PREFIX dist)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ARCH "" CACHE STRING "Specify the target platform architecture")

option(USING_ASAN "Enable AddressSanitizer" OFF)
option(USING_COVERAGE "Enable code coverage" OFF)

if(USING_ASAN)
add_compile_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined)
add_link_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined)
endif()

if(USING_COVERAGE)
add_compile_options(--coverage)
add_link_options(--coverage)
# use lcov generate coverage report
# lcov -c -d . -o coverage.info
# genhtml coverage.info -o coverage_report
endif()

add_subdirectory(sample)
add_subdirectory(crypt)
add_subdirectory(core)
add_subdirectory(third_party)
add_subdirectory(tests)