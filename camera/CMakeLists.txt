cmake_minimum_required(VERSION 3.10)

project(camera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH TRUE)
set(AICAM_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

option(AICAM_ENABLE_RKNN "Enable RKNN runtime integration when headers and libs are available" ON)
option(AICAM_ENABLE_RGA "Enable Rockchip RGA accelerated AI preprocessing when available" ON)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(ROCKIT_ROOT "/home/vanilla/sdk/output/out/media_out" CACHE PATH
    "Rockchip media output root, containing include/ and lib/")

set(RKNN_ROOT "${ROCKIT_ROOT}" CACHE PATH
    "RKNN runtime root, containing rknn_api.h and librknnrt.so")

find_path(ROCKIT_INCLUDE_DIR
    NAMES rk_mpi_venc.h
    HINTS ${ROCKIT_ROOT}
    PATH_SUFFIXES
        include
        usr/include
    NO_CMAKE_FIND_ROOT_PATH
)

find_library(ROCKIT_LIBRARY
    NAMES rockit
    HINTS ${ROCKIT_ROOT}
    PATH_SUFFIXES
        lib
        usr/lib
    NO_CMAKE_FIND_ROOT_PATH
)

find_library(ROCKCHIP_MPP_LIBRARY
    NAMES rockchip_mpp
    HINTS ${ROCKIT_ROOT}
    PATH_SUFFIXES
        lib
        usr/lib
    NO_CMAKE_FIND_ROOT_PATH
)

find_library(DRM_LIBRARY
    NAMES drm
    HINTS
        ${ROCKIT_ROOT}
        ${ROCKIT_ROOT}/root/usr
    PATH_SUFFIXES
        lib
        usr/lib
    NO_CMAKE_FIND_ROOT_PATH
)

find_library(ASOUND_LIBRARY
    NAMES asound
    HINTS
        ${ROCKIT_ROOT}
        ${ROCKIT_ROOT}/root/usr
    PATH_SUFFIXES
        lib
        usr/lib
    NO_CMAKE_FIND_ROOT_PATH
)

find_library(RKAIQ_LIBRARY
    NAMES rkaiq
    HINTS ${ROCKIT_ROOT}
    PATH_SUFFIXES
        lib
        usr/lib
    NO_CMAKE_FIND_ROOT_PATH
)

if(NOT ROCKIT_INCLUDE_DIR OR NOT ROCKIT_LIBRARY OR NOT ROCKCHIP_MPP_LIBRARY OR NOT DRM_LIBRARY OR NOT ASOUND_LIBRARY OR NOT RKAIQ_LIBRARY)
    message(FATAL_ERROR
        "Rockchip media/AIQ libraries not found. Pass -DROCKIT_ROOT=/path/to/sdk/output/out/media_out"
    )
endif()

set(AICAM_RKNN_ENABLED 0)
set(AICAM_RGA_ENABLED 0)
set(AICAM_EXTRA_INCLUDE_DIRS "")
set(AICAM_EXTRA_LIBS "")

if(AICAM_ENABLE_RKNN)
    find_path(RKNN_INCLUDE_DIR
        NAMES rknn_api.h
        HINTS
            ${RKNN_ROOT}
            /home/vanilla/sdk/project/app/rkai/rkai/common
            /home/vanilla/sdk/project/app/testdemo/yoloworld_demo/3rdparty/rknpu2
        PATH_SUFFIXES
            include
            usr/include
        NO_CMAKE_FIND_ROOT_PATH
    )

    find_library(RKNNRT_LIBRARY
        NAMES rknnrt
        HINTS
            ${RKNN_ROOT}
            /home/vanilla/sdk/media/out
            /home/vanilla/sdk/output/out/media_out
        PATH_SUFFIXES
            lib
            usr/lib
        NO_CMAKE_FIND_ROOT_PATH
    )

    if(RKNN_INCLUDE_DIR AND RKNNRT_LIBRARY)
        set(AICAM_RKNN_ENABLED 1)
        list(APPEND AICAM_EXTRA_INCLUDE_DIRS ${RKNN_INCLUDE_DIR})
        list(APPEND AICAM_EXTRA_LIBS ${RKNNRT_LIBRARY})
        message(STATUS "AICAM RKNN support enabled")
    else()
        message(WARNING "RKNN runtime not found. camera will build with AI stub mode.")
    endif()
endif()

if(AICAM_ENABLE_RGA)
    find_path(RGA_INCLUDE_DIR
        NAMES rga/im2d.h
        HINTS ${ROCKIT_ROOT}
        PATH_SUFFIXES
            include
            usr/include
        NO_CMAKE_FIND_ROOT_PATH
    )

    find_library(RGA_LIBRARY
        NAMES rga
        HINTS ${ROCKIT_ROOT}
        PATH_SUFFIXES
            lib
            usr/lib
        NO_CMAKE_FIND_ROOT_PATH
    )

    if(RGA_INCLUDE_DIR AND RGA_LIBRARY)
        set(AICAM_RGA_ENABLED 1)
        list(APPEND AICAM_EXTRA_INCLUDE_DIRS ${RGA_INCLUDE_DIR})
        list(APPEND AICAM_EXTRA_LIBS ${RGA_LIBRARY})
        message(STATUS "AICAM RGA preprocessing enabled")
    else()
        message(WARNING "RGA headers/libraries not found. camera will use CPU preprocessing.")
    endif()
endif()

add_executable(camera
    src/AppConfig.cpp
    src/main.cpp
    src/CameraDevice.cpp
    src/DetectionOverlay.cpp
    src/H264Encoder.cpp
    src/IspController.cpp
    src/RingBuffer.cpp
    ../stream/src/FramedTcpClient.cpp
    ../event/src/EventLogger.cpp
    ../metrics/src/MetricsCollector.cpp
    ../ai/src/RknnDetector.cpp
)

target_include_directories(camera
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AICAM_ROOT_DIR}/common/include
        ${AICAM_ROOT_DIR}/capture/include
        ${AICAM_ROOT_DIR}/encode/include
        ${AICAM_ROOT_DIR}/stream/include
        ${AICAM_ROOT_DIR}/ai/include
        ${AICAM_ROOT_DIR}/event/include
        ${AICAM_ROOT_DIR}/metrics/include
        ${ROCKIT_INCLUDE_DIR}
        ${ROCKIT_INCLUDE_DIR}/rkaiq
        ${AICAM_EXTRA_INCLUDE_DIRS}
)

target_compile_definitions(camera
    PRIVATE
        AICAM_HAS_RKNN=${AICAM_RKNN_ENABLED}
        AICAM_HAS_RGA=${AICAM_RGA_ENABLED}
)

target_link_libraries(camera
    PRIVATE
        ${ROCKIT_LIBRARY}
        ${ROCKCHIP_MPP_LIBRARY}
        ${DRM_LIBRARY}
        ${ASOUND_LIBRARY}
        ${RKAIQ_LIBRARY}
        ${AICAM_EXTRA_LIBS}
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
)
