cmake_minimum_required(VERSION 2.8)

set (TESTS evtest bulktest http_test filter_test router_test router_test2 no_type_router_test filter2_test transform_test auto_test extract_test submit_test thin_test rawtest multi_thread multiq_test)
set (NO_INST_PROGS thin_client)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(${TEST} evpath ${BASE_LIBS})
    ADD_TEST(${TEST} "${TEST}")
endforeach()

ADD_EXECUTABLE(thin_client thin_client.c)
TARGET_LINK_LIBRARIES(thin_client evpath ${BASE_LIBS})

ADD_TEST(evtest_UDP evtest -t udp)
IF (ENET_FOUND)
    ADD_TEST(evtest_ENET evtest -t enet)
    ADD_TEST(bulktest_ENET bulktest -t enet)
ENDIF (ENET_FOUND)
ADD_SUBDIRECTORY( testdll )

