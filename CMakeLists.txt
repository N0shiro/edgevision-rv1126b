cmake_minimum_required(VERSION 3.10)

project(aicam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AICAM_ENABLE_RKNN "Enable RKNN runtime integration when headers and libs are available" ON)

add_subdirectory(camera)
add_subdirectory(camera_gateway)
