cmake_minimum_required(VERSION 2.8)

set (TESTS cmtest cmping bulktest take_test)

if(MPI_C_INCLUDE_PATH)
include_directories(${MPI_C_INCLUDE_PATH})
endif()

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cmtests.tsts "" )
foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(mtests_${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(mtests_${TEST} evpath ${BASE_LIBS})
    ADD_TEST(mtests_${TEST} mtests_${TEST})
    SET_TARGET_PROPERTIES(mtests_${TEST} PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
    LIST( APPEND CMTESTS "mtests_${TEST}")
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cmtests.tsts "mtests_${TEST}\n" )
endforeach()

ADD_TEST (mtests_cmtest_UDP mtests_cmtest -t udp)
ADD_TEST (mtests_cmping_UDP mtests_cmping -t udp)
ADD_EXECUTABLE (trans_test trans_test.c)
SET_TARGET_PROPERTIES(trans_test PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
TARGET_LINK_LIBRARIES(trans_test evpath ${BASE_LIBS})

if (MPI_FOUND)
  ADD_EXECUTABLE (mpi_trans_test trans_test.c)
  SET_TARGET_PROPERTIES(mpi_trans_test PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
  set_target_properties(mpi_trans_test PROPERTIES COMPILE_FLAGS "${MPI_C_COMPILE_FLAGS} -DBUILD_WITH_MPI=1")
  if(MPI_LINK_FLAGS)
    set_target_properties(mpi_trans_test PROPERTIES LINK_FLAGS "${MPI_C_LINK_FLAGS}")
  endif()
  if(MPI_C_LIBRARIES)
    TARGET_LINK_LIBRARIES(mpi_trans_test evpath ${MPI_C_LIBRARIES} ${BASE_LIBS})
  else()
    TARGET_LINK_LIBRARIES(mpi_trans_test evpath ${BASE_LIBS})
  endif()
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
IF (RUN_NNTI_TESTS)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"nnti\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (RUN_NNTI_TESTS)
IF (RUN_ENET_TESTS)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"enet\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (RUN_ENET_TESTS)
IF (RUN_IB_TESTS)
    file (APPEND "${CMAKE_CURRENT_BINARY_DIR}/correctness_spec" 
         "\$test_set{\"ib\"} = \"basic_size_list all_read_buffer_styles all_write_buffer_styles\";\n")
ENDIF (RUN_IB_TESTS)

ADD_TEST (all_transports perl ./transport_test.pl -q -f ./correctness_spec)
if (TEST_INSTALL_DIRECTORY) 
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/run_remote_tests.pl DESTINATION "${TEST_INSTALL_DIRECTORY}")
  install(TARGETS ${CMTESTS} DESTINATION "${TEST_INSTALL_DIRECTORY}")
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cmtests.tsts DESTINATION "${TEST_INSTALL_DIRECTORY}")
endif (TEST_INSTALL_DIRECTORY) 

