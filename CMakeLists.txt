cmake_minimum_required(VERSION 3.28)

project(pentazemin VERSION 1.0 LANGUAGES C ASM)

set(DEBUG "" CACHE STRING "Debug level (1,2,3)")
message(STATUS "DEBUG LEVEL = ${DEBUG}")

include(FetchContent)
FetchContent_Declare(
   arkdevsdk
   GIT_REPOSITORY https://github.com/PSP-Arkfive/ark-dev-sdk.git
   GIT_TAG main
   EXCLUDE_FROM_ALL
)
FetchContent_Declare(
   bootloadex
   GIT_REPOSITORY https://github.com/PSP-Arkfive/BootLoadEx.git
   GIT_TAG main
   EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(arkdevsdk bootloadex)

set(ARKSDK ${arkdevsdk_SOURCE_DIR})

add_prx_module(pentazemin exports.exp)

target_sources(pentazemin PRIVATE 
   main.c
   src/funcs.c
   src/usbcam.c
   src/vitamem.c
   src/syspatch.c
   src/flashfs.c
   src/io_patch.c
   src/loadexec.c
   src/adrenaline.c
   src/init_patch.c
   src/power_patch.c
   src/LflashFatfmt.c
   src/impose_patch.c
   src/utility_patch.c
   src/vlf.c
   imports.S
   payload.h
)

remove_definitions("-D_PSP_FW_VERSION=600")
add_definitions("-D_PSP_FW_VERSION=660")

target_compile_options(pentazemin PRIVATE -std=c99 -Os -G0 -Wall -fno-pic)
target_include_directories(pentazemin PRIVATE include ${ARKSDK}/include ${CMAKE_CURRENT_BINARY_DIR})
target_link_directories(pentazemin PRIVATE ${ARKSDK}/libs)
target_link_libraries(pentazemin PRIVATE
   -nostartfiles
   -nostdlib
   pspsystemctrl_kernel
   pspkermit_driver
   pspkermitperipheral_driver
   pspsdk
   pspmodinfo
   pspkernel
)

add_custom_command(OUTPUT payload.h
  DEPENDS pentazemin_reboot
  COMMAND bin2c "$<TARGET_FILE:pentazemin_reboot>.bin.gz" payload.h rebootbuffer_pentazemin
)

add_subdirectory(rebootex)