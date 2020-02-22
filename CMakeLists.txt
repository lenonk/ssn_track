cmake_minimum_required(VERSION 3.0)
project(ssn_track VERSION 0.1)
configure_file(version.h.in version.h)

add_subdirectory(bgh)
add_subdirectory(dsh)
add_subdirectory(tests)
