cmake_minimum_required(VERSION 3.22)
project(unittest)

add_executable(unittest test.cpp)
target_sources(unittest PRIVATE unittests/buritto_test.cpp)
target_include_directories(unittest PRIVATE include)
target_link_libraries(unittest buritto pthread)

