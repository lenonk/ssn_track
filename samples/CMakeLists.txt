cmake_minimum_required(VERSION 2.8)

add_executable(pcap_stats pcap_stats.cc)

include_directories(pcap_stats ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
link_directories(pcap_stats ${PROJECT_SOURCE_DIR})
target_link_libraries(pcap_stats ssn_track pcap)

