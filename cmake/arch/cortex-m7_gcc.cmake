set(ARCH_FLAGS " \
  -mcpu=cortex-m7 \
  -mfpu=fpv5-d16 \
  -mfloat-abi=hard \
  -mthumb \
  -fno-common \
  -fmessage-length=0 \
  -Wall \
  -fno-exceptions \
  -ffunction-sections \
  -fdata-sections \
  -fomit-frame-pointer \
  -fshort-enums \
  -Wa,-mimplicit-it=thumb \
")

include(cmake/toolchain/arm-none-eabi-gcc.cmake)

set(CMAKE_SYSTEM_PROCESSOR Cortex-M7)
