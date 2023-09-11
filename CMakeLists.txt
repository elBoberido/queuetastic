cmake_minimum_required(VERSION 3.22)
project(queuetastic)

add_compile_options(-std=c++14)
add_compile_options(-O2 -g3)
add_compile_options(-latomic)
add_compile_options(-W -Wall -pedantic -Wextra -Wconversion -Wuninitialized -Wno-keyword-macro)

add_subdirectory(buritto)
add_subdirectory(test)
