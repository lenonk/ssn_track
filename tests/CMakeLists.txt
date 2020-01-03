cmake_minimum_required(VERSION 3.0)

#add_executable(tests test.cc)

include_directories(tests ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
link_directories(tests ${PROJECT_SOURCE_DIR})
#target_link_libraries(tests ssn_track)

add_executable(tests_hd test_hd.cc)

include_directories(tests_hd ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
link_directories(tests_hd ${PROJECT_SOURCE_DIR})
target_link_libraries(tests_hd ssn_track_hd pthread)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -std=c++11")
