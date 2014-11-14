cmake_minimum_required(VERSION 2.8)

set (TESTS cmtest cmping bulktest take_test)

if(MPI_C_INCLUDE_PATH)
include_directories(${MPI_C_INCLUDE_PATH})
endif()

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(mtests_${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(mtests_${TEST} evpath ${BASE_LIBS})
    ADD_TEST(mtests_${TEST} mtests_${TEST})
    SET_TARGET_PROPERTIES(mtests_${TEST} PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
endforeach()

ADD_TEST (mtests_cmtest_UDP mtests_cmtest -t udp)
ADD_TEST (mtests_cmping_UDP mtests_cmping -t udp)
ADD_EXECUTABLE (trans_test trans_test.c)
SET_TARGET_PROPERTIES(trans_test PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
if(MPI_COMPILE_FLAGS)
  set_target_properties(trans_test PROPERTIES COMPILE_FLAGS "${MPI_C_COMPILE_FLAGS}")
endif()
if(MPI_LINK_FLAGS)
  set_target_properties(trans_test PROPERTIES LINK_FLAGS "${MPI_C_LINK_FLAGS}")
endif()
if(MPI_C_LIBRARIES)
  TARGET_LINK_LIBRARIES(trans_test evpath ${MPI_C_LIBRARIES} ${BASE_LIBS})
else()
  TARGET_LINK_LIBRARIES(trans_test evpath ${BASE_LIBS})
endif()
configure_file(transport_test.pl transport_test.pl COPYONLY)
configure_file(test_spec test_spec COPYONLY)
file (WRITE "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
"%macro = (\n"
"        basic_size_list  => [ \"size:20 vectors:1 msg_count:10\",\n"
"			      \"size:102400 vectors:1 msg_count:3\" ],\n"
"        tiny_size_list  => [ \"size:20 vectors:1 msg_count:10 \"],\n"
"	all_read_buffer_styles  => [ \"take_receive_buffer:0\", \"take_receive_buffer:1\" ],\n"
"	all_write_buffer_styles  => [ \"reuse_write_buffers:0\", \"reuse_write_buffers:1\" ],\n"
"      );\n")
file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
"\$test_set{\"sockets\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
IF (NNTI_FOUND)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"nnti\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (NNTI_FOUND)
IF (ENET_FOUND)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"enet\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (ENET_FOUND)
IF (IB_FOUND)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"ib\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (IB_FOUND)

ADD_TEST (all_transports perl ./transport_test.pl -q -f ./correctness_spec)
