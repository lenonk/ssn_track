cmake_minimum_required(VERSION 3.0)

project(ssn_track VERSION 0.1)

configure_file(version.h.in version.h)

#target_include_directories(ssn_track PUBLIC "${PROJECT_BINARY_DIR}")
add_library(ssn_track ssn_track.c)
add_subdirectory(samples)
add_subdirectory(tests)
target_link_libraries(ssn_track crypto)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
