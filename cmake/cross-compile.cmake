cmake_minimum_required(VERSION 3.10)

message(STATUS "Host system: ${CMAKE_HOST_SYSTEM_NAME}")
if(ARCH)
include(cmake/arch/${ARCH}.cmake)
endif()
