cmake_minimum_required(VERSION 3.0)
project(buritto)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g3" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -latomic" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -pedantic -Wextra -Wconversion -Wuninitialized -Wno-keyword-macro" )

add_executable(buritto src/main.cpp)

target_link_libraries(buritto pthread)
