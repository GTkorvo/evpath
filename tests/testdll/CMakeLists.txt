cmake_minimum_required(VERSION 2.8)

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_library(foo SHARED foo.c)
