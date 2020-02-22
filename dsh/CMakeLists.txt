cmake_minimum_required(VERSION 3.0)

project(dsh VERSION 0.1)

add_library(dsh dsh.c)
target_link_libraries(dsh crypto)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
