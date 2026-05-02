set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_CROSSCOMPILING TRUE)

# Luckfox Aura / RV1126B 当前设备实测为 aarch64 用户态环境
# 工具链前缀不带结尾的短横线
set(TOOLCHAIN_PREFIX "aarch64-linux-gnu" CACHE STRING "Cross compiler prefix")

# 交叉编译器所在目录；如果已经在 PATH 中，可以留空
set(TOOLCHAIN_BIN_DIR "" CACHE PATH "Directory containing cross compiler binaries")

# 目标板 sysroot 路径；如果工具链已经自带完整 sysroot，可以传进来
set(CMAKE_SYSROOT "" CACHE PATH "Target sysroot")

# 目标板上的 x264 安装前缀，目录下应包含 include/ 和 lib/
set(X264_ROOT "" CACHE PATH "Prefix path of target x264")

# RV1126B 使用 Cortex-A53，属于 ARMv8-A / AArch64
set(RV1126_ARCH_FLAGS "-mcpu=cortex-a53" CACHE STRING "Architecture flags")

if(TOOLCHAIN_BIN_DIR)
    file(TO_CMAKE_PATH "${TOOLCHAIN_BIN_DIR}" _toolchain_bin_dir)
    set(_toolchain_prefix "${_toolchain_bin_dir}/${TOOLCHAIN_PREFIX}")
else()
    set(_toolchain_prefix "${TOOLCHAIN_PREFIX}")
endif()

set(CMAKE_C_COMPILER   "${_toolchain_prefix}-gcc" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_toolchain_prefix}-g++" CACHE FILEPATH "C++ compiler" FORCE)

set(CMAKE_C_FLAGS_INIT   "${RV1126_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${RV1126_ARCH_FLAGS}")

if(CMAKE_SYSROOT)
    set(CMAKE_SYSROOT_COMPILE "${CMAKE_SYSROOT}" CACHE PATH "Compile sysroot" FORCE)
    set(CMAKE_SYSROOT_LINK    "${CMAKE_SYSROOT}" CACHE PATH "Link sysroot" FORCE)
endif()

set(CMAKE_FIND_ROOT_PATH "")

if(CMAKE_SYSROOT)
    list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
endif()

if(X264_ROOT)
    list(APPEND CMAKE_FIND_ROOT_PATH "${X264_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${X264_ROOT}")
endif()

# 程序在宿主机找；库、头文件、包配置在目标机根路径找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
