cmake_minimum_required(VERSION 3.10)

project(camera_gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(camera_gateway
    src/main.cpp
    src/TcpServer.cpp
)

target_include_directories(camera_gateway
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(camera_gateway
    PRIVATE
        Threads::Threads
)
