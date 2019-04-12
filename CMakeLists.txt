cmake_minimum_required(VERSION 3.7.2)
project(scratchsocket)

set(TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SCRATCHSOCKET_SRC_DIR ${TOP_DIR}/src)

include_directories(${SCRATCHSOCKET_SRC_DIR})
aux_source_directory(${SCRATCHSOCKET_SRC_DIR} SCRATCHSOCKET_SRCS)

add_executable(scratchsocket ${SCRATCHSOCKET_SRCS})
target_link_libraries(scratchsocket ws2_32)
