cmake_minimum_required(VERSION 3.2)
project(cache_calculator)

set(CMAKE_CXX_STANDARD 20)

add_executable(cache_calculator main.cpp)
set(CMAKE_CXX_FLAGS -O3)
add_compile_definitions(DEBUG)
