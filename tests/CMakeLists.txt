cmake_minimum_required(VERSION 3.0)

add_executable(tests test.cc)

include_directories(tests ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
link_directories(tests ${PROJECT_SOURCE_DIR})
target_link_libraries(tests ssn_track)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")
