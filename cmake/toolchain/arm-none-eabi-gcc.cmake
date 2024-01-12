set(ARM_TOOLCHAIN_DIR "" CACHE STRING "Specify the GNU ARM toolchain directory")

#设置目标机使用的操作系统名称，如果没有就写 Generic，执行该指令后，cmake 就知道现在执行的是交叉编译
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

if (MINGW OR CYGWIN OR WIN32)
  set(SEARCH_CMD where)
elseif (UNIX OR APPLE)
  set(SEARCH_CMD which)
endif ()

set(TOOLCHAIN_PREFIX arm-none-eabi-)

if (NOT ARM_TOOLCHAIN_DIR)
  execute_process(
    COMMAND ${SEARCH_CMD} ${TOOLCHAIN_PREFIX}gcc
    OUTPUT_VARIABLE BINUTILS_PATH #执行程序标准输出用 BINUTILS_PATH 来保存
    OUTPUT_STRIP_TRAILING_WHITESPACE #删除运行命令的标准输出中的任何尾随空格
  )
  if (NOT BINUTILS_PATH)
    message(FATAL_ERROR "Cannot find toolchain path.")
  endif ()
  get_filename_component(ARM_TOOLCHAIN_DIR ${BINUTILS_PATH} DIRECTORY)
endif ()

if (CMAKE_HOST_WIN32)
  set(TOOLCHAIN_SUFFIX ".exe")
endif ()

#设置编译器路径
set(CMAKE_C_COMPILER ${ARM_TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
#用于设置可执行文件名末尾的后缀
set(CMAKE_EXECUTABLE_SUFFIX_C .elf)
set(CMAKE_EXECUTABLE_SUFFIX_CXX .elf)
set(CMAKE_EXECUTABLE_SUFFIX_ASM .elf)
#设置 C 编译器编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}")
#设置 C++ 编译器编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}")
#设置汇编器编译选项
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp ${ARCH_FLAGS}")
#设置链接选项
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -nostdlib ${ARCH_FLAGS}")
# 设置编译器相关库文件的变更，比如编译器自带的c库，由于是交叉编译，因此需要设置这个路径，默认路径是 /usr/lib 等
set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

enable_language(C CXX ASM)
