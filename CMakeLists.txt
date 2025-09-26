cmake_minimum_required(VERSION 3.31)
project(virtual_machines)

set(CMAKE_CXX_STANDARD 20)

add_executable(virtual_machines main.cpp)
set(CMAKE_CXX_FLAGS -O3)
