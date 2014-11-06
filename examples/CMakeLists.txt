cmake_minimum_required(VERSION 2.8)

set (NO_INST_PROGS triv net_send net_recv multi_send derived_send derived_recv 
	transform_recv derived_send2 transform_recv2 transform_recv3
	dfg_master dfg_client dfg_master2 dfg_client3 dfg_master3 dfg_master4)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (PROG ${NO_INST_PROGS} )
    ADD_EXECUTABLE(${PROG} ${PROG}.c)
    SET_TARGET_PROPERTIES(${PROG} PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
    TARGET_LINK_LIBRARIES(${PROG} evpath ${BASE_LIBS})
endforeach()

