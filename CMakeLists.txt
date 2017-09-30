cmake_minimum_required(VERSION 3.0)
project(buritto)

add_definitions(-std=c++11)

add_executable(buritto src/main.cpp)

target_link_libraries(buritto pthread)
