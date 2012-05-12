cmake_minimum_required(VERSION 2.8)

set (NO_INST_PROGS triv net_send net_recv multi_send derived_send derived_recv 
	transform_recv derived_send2 transform_recv2 transform_recv3)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (PROG ${NO_INST_PROGS} )
    ADD_EXECUTABLE(${PROG} ${PROG}.c)
    TARGET_LINK_LIBRARIES(${PROG} evpath)
endforeach()

