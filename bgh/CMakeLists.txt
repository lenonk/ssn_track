cmake_minimum_required(VERSION 3.0)

add_library(bgh bgh.c)
target_link_libraries(bgh crypto pthread)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
