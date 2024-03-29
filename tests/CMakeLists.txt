include(GoogleTest)

function(add_test_framework TARGET)
    file(GLOB SOURCES *.cpp **/*.cpp)
    file(GLOB OBJECT_HEADERS *.h **/*.h)

    add_executable(${TARGET} ${SOURCES})
    gtest_discover_tests(${TARGET})
    target_link_libraries(${TARGET} gtest_main)
    target_link_libraries(${TARGET} benchmark)
endfunction()

macro(subdir_list result curdir)
  file(GLOB children RELATIVE ${curdir} ${curdir}/*)
  set(dirlist "")
  foreach (child ${children})
    if (IS_DIRECTORY ${curdir}/${child})
      list(APPEND dirlist ${child})
    endif ()
  endforeach ()
  set(${result} ${dirlist})
endmacro()

subdir_list(SUBDIRS ${CMAKE_CURRENT_LIST_DIR})
foreach (subdir ${SUBDIRS})
  if (NOT ("${subdir}" STREQUAL "common")) #公共目录，这个目录可以添加一些功能，供其他测试用例使用
    add_subdirectory(${subdir})
  endif ()
endforeach ()
