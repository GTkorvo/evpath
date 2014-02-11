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
endforeach()

ADD_TEST (mtests_cmtest_UDP mtests_cmtest -t udp)
ADD_TEST (mtests_cmping_UDP mtests_cmping -t udp)
ADD_EXECUTABLE (trans_test trans_test.c)
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
