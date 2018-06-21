cmake_minimum_required(VERSION 3.0)
project(buritto)

add_compile_options(-std=c++11)
add_compile_options(-O2 -g3)
add_compile_options(-latomic)
add_compile_options(-W -Wall -pedantic -Wextra -Wconversion -Wuninitialized -Wno-keyword-macro)

# add_executable(buritto src//main.cpp)
add_executable(buritto test/test.cpp)
target_sources(buritto PRIVATE test/unittests/buritto_test.cpp)
target_include_directories(buritto PRIVATE src)
target_include_directories(buritto PRIVATE test/include)
target_link_libraries(buritto pthread)
