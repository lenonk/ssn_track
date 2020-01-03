cmake_minimum_required(VERSION 3.0)

project(ssn_track VERSION 0.1)

configure_file(version.h.in version.h)

add_library(ssn_track ssn_track.c)
add_subdirectory(samples)
add_subdirectory(tests)
target_link_libraries(ssn_track crypto)

add_library(ssn_track_hd ssn_track_hd.c)
target_link_libraries(ssn_track_hd crypto)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
