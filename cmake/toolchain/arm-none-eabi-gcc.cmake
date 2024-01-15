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
# cmake 会自己运行一个叫做 "try_compile" 命令，
# 用于确定编译器是正常工作的，这里设置尝试编译生成目标，交叉编译一般需要更新设置这个变量
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
#设置 C 编译器编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}")
#设置 C++ 编译器编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}")
#设置汇编器编译选项
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp ${ARCH_FLAGS}")
#设置链接选项
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -nostdlib ${ARCH_FLAGS}")
# 由于是交叉编译，编译器路径是变了的，编译器下相关的库路径也是变了的
# 这里重新设置 cmake 查找路径，这样 cmake 会从这个位置查找相关库文件，以进行编译链接
# 这里找不到会去 usr/lib 等目录下找
set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

enable_language(C CXX ASM)
