cmake_minimum_required(VERSION 2.8)

configure_file(version.h.in version.h)

#target_include_directories(ssn_track PUBLIC "${PROJECT_BINARY_DIR}")
add_library(ssn_track ssn_track.cc)
add_subdirectory(samples)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
