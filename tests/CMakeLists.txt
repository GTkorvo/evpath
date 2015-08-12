cmake_minimum_required(VERSION 2.8)

set (TESTS evtest bulktest http_test filter_test router_test router_test2 no_type_router_test filter2_test transform_test auto_test extract_test submit_test thin_test rawtest rawtest2 multi_thread multiq_test split_test executing_stone_test metrics_test)
set (NO_INST_PROGS thin_client)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/evtests.tsts "" )
foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(${TEST} ${TEST}.c)
    SET_TARGET_PROPERTIES(${TEST} PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
    TARGET_LINK_LIBRARIES(${TEST} evpath ${BASE_LIBS})
    ADD_TEST(${TEST} "${TEST}")
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/evtests.tsts "${TEST}\n" )
endforeach()

ADD_TEST(evtest_SELF "evtest")
set_property(TEST evtest_SELF PROPERTY
    ENVIRONMENT "CMSelfFormats=1"
  )
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/evtests.tsts "CMSelfFormats=1 evtest\n" )

ADD_EXECUTABLE(thin_client thin_client.c)
SET_TARGET_PROPERTIES(thin_client PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
TARGET_LINK_LIBRARIES(thin_client evpath ${BASE_LIBS})

ADD_TEST(evtest_UDP evtest -t udp)
IF (ENET_FOUND)
    ADD_TEST(evtest_ENET evtest -t enet)
    ADD_TEST(bulktest_ENET bulktest -t enet)
ENDIF (ENET_FOUND)
IF(NOT (BUILD_SHARED_STATIC EQUAL "STATIC"))
    ADD_SUBDIRECTORY( testdll )
ENDIF()

if (TEST_INSTALL_DIRECTORY) 
  install(TARGETS ${TESTS} thin_client DESTINATION "${TEST_INSTALL_DIRECTORY}")
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evtests.tsts DESTINATION "${TEST_INSTALL_DIRECTORY}")
endif (TEST_INSTALL_DIRECTORY) 
