cmake_minimum_required(VERSION 3.15)
project(test VERSION 1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog REQUIRED)

add_executable(test test.cpp)

target_link_libraries(test PRIVATE spdlog::spdlog)
